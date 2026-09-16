#pragma once

#include <cstdint>
#include <cassert>
#include "util/DebugFormatter.hpp"
#include "util/InterruptController.hpp"
#include "util/EventTimer.hpp"
#include "NClock.hpp"
#include "serial_devices/SerialDevice.hpp"
#include "serial_devices/host/HostSerial.hpp"

constexpr bool SCDEBUG = 0;

/**
 * this code is LITTLE-ENDIAN SPECIFIC.
 * IF you have a big endian machine, you will need to swap the bytes. Or cry.
 */

/* Timing Clock fed into SCC by the IIgs */
constexpr uint64_t SCC_RX_CLOCK = 3'686'400;
// FORK: the other clock input. Docs/SCC8530_Serial.md's schematic reading has PCLK on CREF,
// next to 14M and 7M, most likely the 3.579545 MHz colour reference (unverified). Only a
// program that sets WR14 D1 (BRG source = PCLK) sees it.
constexpr uint64_t SCC_PCLK_HZ = 3'579'545;

/* Emulator master clock (14M clock) */
constexpr uint64_t MASTER_CLOCK = 14'318'180;

enum scc_channel_t {
    SCC_CHANNEL_A,
    SCC_CHANNEL_B,
    SCC_CHANNEL_COUNT,
};

enum scc_register_t {
    WR0 = 0,
    WR1 = 1,
    WR2 = 2,
    WR3 = 3,
    WR4 = 4,
    WR5 = 5,
    WR6 = 6,
    WR7 = 7,
    WR8 = 8,
    WR9 = 9,
    WR10 = 10,
    WR11 = 11,
    WR12 = 12,
    WR13 = 13,
    WR14 = 14,
    WR15 = 15,
    RR0 = 0,
    RR1 = 1,
    RR2 = 2,
    RR3 = 3,
    RR4 = 4,
    RR5 = 5,
    RR6 = 6,
    RR7 = 7,
    RR8 = 8,
    RR9 = 9,
    RR10 = 10,
    RR11 = 11,
    RR12 = 12,
    RR13 = 13,
    RR14 = 14,
    RR15 = 15,
};

class Z85C30 {
    public:
        // Observatory: the per-channel RR/WR register file as a coverage-first window.
        const void* obs_reg_window() const { return &registers[0]; }
        uint32_t    obs_reg_len()    const { return (uint32_t)sizeof(registers); }
    private:
    InterruptController *irq_control = nullptr;
    EventTimer *event_timer = nullptr;
    NClockII *clock = nullptr;

    /* FILE *data_files[SCC_CHANNEL_COUNT] = { NULL, NULL }; */

    inline char ch_name(scc_channel_t channel) {
        return (uint8_t) channel + 'A';
    }
    inline char ch_name(int channel) {
        return (char)('A' + channel);
    }

    SerialDevice *serial_devices[SCC_CHANNEL_COUNT] = { nullptr, nullptr };

    struct scc_channel_state_t {
        uint8_t char_rx;
        uint8_t char_tx;
        bool tx_in_progress = false;  // TX character being transmitted
        bool rx_in_progress = false;  // RX character being received
        union {
            uint8_t r_reg_0;
            struct {
                uint8_t r0_rx_char_available: 1;
                uint8_t r0_zero_count: 1;
                uint8_t r0_tx_buffer_empty: 1;
                uint8_t r0_dcd: 1;
                uint8_t r0_sync_hunt: 1;
                uint8_t r0_cts: 1;
                uint8_t r0_tx_underrun_eom: 1;
                uint8_t r0_break_abort: 1;
            };
        };
        union {
            uint8_t r_reg_1;
            struct {
                uint8_t r1_all_sent: 1;
                uint8_t r1_residue_code: 3;
                uint8_t r1_parity_err: 1;
                uint8_t r1_rx_overrun_err: 1;
                uint8_t r1_crc_framing_err: 1;
                uint8_t r1_end_of_frame: 1;
            };
        };
        //uint8_t r_reg_2;
        union {
            uint8_t r_reg_3;
            struct {
                uint8_t r3_b_ext_pending: 1;
                uint8_t r3_b_tx_pending: 1;
                uint8_t r3_b_rx_pending: 1;
                uint8_t r3_a_ext_pending: 1;
                uint8_t r3_a_tx_pending: 1;
                uint8_t r3_a_rx_pending: 1;
                uint8_t r3_unused: 2;
            };
        };
        union {
            uint8_t r_reg_10;
            struct {
                uint8_t r10_un1: 1;
                uint8_t r10_on_loop: 1;
                uint8_t r10_un2: 2;
                uint8_t r10_loop_sending: 1;
                uint8_t r10_un3: 1;
                uint8_t r10_two_clock_missing: 1;
                uint8_t r10_one_clock_missing: 1;
            };
        };
        // RR15 is the same as WR15 with a mask of 0b1111'1010;

        bool tx_irq_condition = false;
        union {
            uint8_t w_regs[16];
            struct {
                /* Reg 0 */
                uint8_t r0_reg_num: 3;
                uint8_t r0_cmd: 3;
                uint8_t r0_reset_cmd: 2;

                /* Reg 1 */
                uint8_t r1_ext_int_enable: 1;
                uint8_t r1_tx_int_enable: 1;
                uint8_t r1_partity_spcond: 1;
                uint8_t r1_rx_int_mode: 2;
                uint8_t r1_dma_req_on_rx: 1;
                uint8_t r1_dma_req_func: 1;
                uint8_t r1_dma_req: 1;

                uint8_t r2_int_vector: 8;

                /* Reg 3 */
                uint8_t r3_rx_enable: 1;
                uint8_t r3_sync_char_load_inhibit: 1;
                uint8_t r3_address_search_mode: 1;
                uint8_t r3_rx_crc_enable: 1;
                uint8_t r3_hunt_mode: 1;
                uint8_t r3_auto_enables: 1;
                uint8_t r3_rx_bits_per_char: 2;

                /* Reg 4 */
                uint8_t r4_parity_enable: 1;
                uint8_t r4_parity_even_odd: 1;
                uint8_t r4_stop_bits: 2;
                uint8_t r4_sync_char_mode: 2;
                uint8_t r4_clock_mode: 2;

                /* Reg 5 */
                uint8_t r5_tx_crc_enable: 1;
                uint8_t r5_rts: 1;
                uint8_t r5_sdlc_crc16: 1;
                uint8_t r5_tx_enable: 1;
                uint8_t r5_send_break: 1;
                uint8_t r5_tx_bits_per_char: 2;
                uint8_t r5_dtr: 1;

                /* Reg 6 */
                uint8_t r6: 8;

                /* Reg 7 */
                uint8_t r7_auto_tx_flag: 1;
                uint8_t r7_auto_eom_reset: 1;
                uint8_t r7_auto_rts_deactivate: 1;
                uint8_t r7_force_txd_high: 1;
                uint8_t r7_dtr_fast_mode: 1;
                uint8_t r7_complete_crc: 1;
                uint8_t r7_extended_read_enable: 1;
                uint8_t r7_reserved: 1;

                /* Reg 8 */
                uint8_t r8: 8;

                /* Reg 9 */
                uint8_t r9_vis: 1;
                uint8_t r9_nv: 1;
                uint8_t r9_dlc: 1;
                uint8_t r9_mie: 1;
                uint8_t r9_status_high_low: 1;
                uint8_t r9_software_intack_enable: 1;
                uint8_t r9_reset: 2;

                /* Reg 10 */
                uint8_t r10_6bit_8bit_sync: 1;
                uint8_t r10_loop_mode: 1;
                uint8_t r10_abort_underrun: 1;
                uint8_t r10_mark_flag_idle: 1;
                uint8_t r10_active_on_poll: 1;
                uint8_t r10_encoding: 2;
                uint8_t r10_crc_preset: 1;

                /* Reg 11 */
                uint8_t r11_txrc_out: 2;
                uint8_t r11_txrc_01: 1;
                uint8_t r11_tx_clock: 2;
                uint8_t r11_rx_clock: 2;
                uint8_t r11_txc_no_xtal: 1;

                /* Reg 12 - 13 */
                uint16_t r12_time_constant: 16;

                /* Reg 14 */
                uint8_t r14_br_gen_enable: 1;
                uint8_t r14_br_gen_source: 1;
                uint8_t r14_dtr_request_func: 1;
                uint8_t r14_auto_echo: 1;
                uint8_t r14_local_loopback: 1;
                uint8_t r14_command: 3;

                /* Reg 15 */
                uint8_t r15_unused: 1;
                uint8_t r15_zero_count_ie: 1;
                uint8_t r15_sdlc_fifo_enable: 1;
                uint8_t r15_dcd_ie: 1;
                uint8_t r15_sync_hunt_ie: 1;
                uint8_t r15_cts_ie: 1;
                uint8_t r15_tx_underrun_eom_ie: 1;
                uint8_t r15_break_abort_ie: 1;
            };
        };
    };

    protected:
        scc_channel_state_t registers[SCC_CHANNEL_COUNT];
        uint16_t reg_select[SCC_CHANNEL_COUNT];
        float baud_rate[SCC_CHANNEL_COUNT];
        uint32_t clock_mode[SCC_CHANNEL_COUNT];

        // Event timer instance IDs (must be unique across the system)
        uint64_t tx_timer_id[SCC_CHANNEL_COUNT];
        uint64_t rx_timer_id[SCC_CHANNEL_COUNT];

        const uint16_t clock_mode_table[4] = { 1, 16, 32, 64 };

        // FORK: THE INTERRUPT BLOCK, laid out as the Z8530 datasheet lays it out.
        // One chip-level IP register in RR3A's bit order, which is also the priority
        // order (A Rx, A Tx, A Ext, B Rx, B Tx, B Ext, highest first), and a matching
        // IUS register. Replaces per-channel bits that lived in A's RR3 while the vector
        // read B's copy, a Tx IP that the next status read consumed, a priority coded
        // Rx > Ext > Tx, and an INT line computed from the accessed channel only.
        uint8_t ip_bits = 0;
        uint8_t ius_bits = 0;
        bool tx_ip_latch[SCC_CHANNEL_COUNT] = { false, false };   // set when the Tx buffer empties
        bool ext_ip_latch[SCC_CHANNEL_COUNT] = { false, false };  // set by an enabled External/Status change
        bool rx_first_armed[SCC_CHANNEL_COUNT] = { true, true };  // Rx mode 01: first character re-armed by WR0 cmd 100

        // FORK: THE RECEIVER AND TRANSMITTER, as the datasheet lays them out (fidelity study,
        // serial step 2).
        //   Receive: wire -> shift register (rx_in_progress, char_rx) -> 3-byte FIFO. A host
        //   byte is taken whenever the shift register is idle -- at a register access, when
        //   the previous character completes, and on a 1 ms emulated-time poll -- instead of
        //   only at the guest's next access with the buffer empty, which stalled a guest
        //   waiting for its Rx interrupt and made arrival time equal access time. A fourth
        //   character with the FIFO full overwrites the newest entry and sets Rx Overrun
        //   (latched until Error Reset; the datasheet flags the overwritten character itself,
        //   which reaches RR1 when it is read -- not modelled). The loopback path used to
        //   overrun after one byte.
        //   Transmit: transmit buffer (char_tx) -> shift register (tx_in_progress, tx_shift)
        //   -> wire. A write into an idle transmitter moves straight to the shift register, so
        //   Tx Buffer Empty (RR0 D2) and Tx IP return at once and a second character can wait
        //   in the buffer; All Sent (RR1 D0) follows the shift register. All Sent used to be a
        //   copy of Tx Buffer Empty, and Tx IP waited for the whole character time.
        struct rx_fifo_t { uint8_t data[3]; uint8_t count; };
        rx_fifo_t rx_fifo[SCC_CHANNEL_COUNT] = {};
        uint8_t tx_shift[SCC_CHANNEL_COUNT] = { 0, 0 };
        uint64_t rx_poll_id[SCC_CHANNEL_COUNT];
        bool rx_poll_scheduled[SCC_CHANNEL_COUNT] = { false, false };
        static constexpr uint64_t RX_POLL_14M = MASTER_CLOCK / 1000;   // 1 ms of 14M time

        // FORK: CLOCKS AS WR11 AND WR14 ROUTE THEM (fidelity study, serial step 6). The Rx and
        // Tx clocks each come from the RTxC pin (the 3.6864 MHz crystal), the TRxC pin (not
        // known to be driven on a IIgs), the baud-rate generator or the DPLL (taken as the BRG
        // here). The BRG divides its source (WR14 D1: RTxC or PCLK) by 2*(TC+2) and runs only
        // while WR14 D0 enables it. A character takes bits * clock mode * divider source
        // periods, computed in whole 14M ticks -- no float, and no rate above which timing
        // stopped (115200 baud used to complete instantly). With no clock a character does not
        // complete: it waits (stalled) until the routing gives it one, as on the chip.
        bool tx_stalled[SCC_CHANNEL_COUNT] = { false, false };
        bool rx_stalled[SCC_CHANNEL_COUNT] = { false, false };
        uint64_t tx_due_14m[SCC_CHANNEL_COUNT] = { 0, 0 };
        uint64_t rx_due_14m[SCC_CHANNEL_COUNT] = { 0, 0 };
        bool trxc_reported = false;

        // FORK: EXTERNAL/STATUS AND THE MODEM LINES (serial steps 4 and 6). DCD, CTS and break
        // come from the attached device (MESSAGE_MODEM_STATUS, MESSAGE_BREAK_ON/OFF; a device
        // that sends none leaves DCD and CTS asserted, as before). A change of a source WR15
        // enables (D1 zero count, D3 DCD, D5 CTS, D7 break -- the same positions as RR0) latches
        // RR0's status bits and, with WR1 D0, raises the Ext/Status IP; Reset Ext/Status
        // re-opens the latch and interrupts again at once if the lines moved meanwhile. DTR,
        // RTS and break go to the device when WR5 changes them, and line parameters only when
        // they change.
        bool dcd_in[SCC_CHANNEL_COUNT] = { true, true };
        bool cts_in[SCC_CHANNEL_COUNT] = { true, true };
        bool break_in[SCC_CHANNEL_COUNT] = { false, false };
        bool zero_count[SCC_CHANNEL_COUNT] = { false, false };
        bool ext_latched[SCC_CHANNEL_COUNT] = { false, false };
        uint8_t ext_latched_bits[SCC_CHANNEL_COUNT] = { 0, 0 };
        uint8_t ext_last_bits[SCC_CHANNEL_COUNT] = { 0x28, 0x28 };
        uint8_t line_ctl_last[SCC_CHANNEL_COUNT] = { 0xFF, 0xFF };
        uint8_t break_out_last[SCC_CHANNEL_COUNT] = { 0xFF, 0xFF };
        uint64_t line_params_last[SCC_CHANNEL_COUNT] = { ~0ull, ~0ull };
        uint64_t zc_timer_id[SCC_CHANNEL_COUNT];
        bool zc_scheduled[SCC_CHANNEL_COUNT] = { false, false };

        inline uint8_t get_bits_per_char(scc_channel_t channel, bool is_tx) {
            // Start bit (always 1)
            uint8_t bits = 1;

            // Data bits (5, 6, 7, or 8)
            uint8_t bits_code = is_tx ? registers[channel].r5_tx_bits_per_char : registers[channel].r3_rx_bits_per_char;
            switch (bits_code) {
                case 0b00: bits += 5; break;
                case 0b01: bits += 7; break;
                case 0b10: bits += 6; break;
                case 0b11: bits += 8; break;
            }

            // Parity bit (if enabled)
            if (registers[channel].r4_parity_enable) {
                bits += 1;
            }

            // Stop bits (1, 1.5, or 2)
            switch (registers[channel].r4_stop_bits) {
                case 0b00: bits += 1; break;      // 1 stop bit
                case 0b01: bits += 1; break;      // 1 stop bit (sync mode, but treat as async)
                case 0b10: bits += 2; break;      // 1.5 stop bits (round up to 2)
                case 0b11: bits += 2; break;      // 2 stop bits
            }

            return bits;
        }

        // FORK: the clock for one direction: its source frequency and the divider in front of
        // the clock mode. False when nothing clocks it (TRxC, or a BRG that is not enabled).
        inline bool clock_source(scc_channel_t channel, bool is_tx, uint64_t &hz, uint64_t &div) {
            const auto &r = registers[channel];
            const uint8_t sel = is_tx ? r.r11_tx_clock : r.r11_rx_clock;
            div = 1;
            switch (sel) {
                case 0b00: hz = SCC_RX_CLOCK; return true;             // RTxC pin: the crystal
                case 0b01:                                               // TRxC pin
                    if (!trxc_reported) {
                        fprintf(stderr, "SCC: a channel clocks from the TRxC pin, which nothing drives here; its characters wait\n");
                        trxc_reported = true;
                    }
                    hz = 0; return false;
                default:                                                 // BRG output (and the DPLL, taken as the BRG)
                    if (!r.r14_br_gen_enable) { hz = 0; return false; }
                    hz = r.r14_br_gen_source ? SCC_PCLK_HZ : SCC_RX_CLOCK;
                    div = 2ull * ((uint64_t)r.r12_time_constant + 2ull);
                    return true;
            }
        }

        // FORK: 14M ticks for one character, or false when the direction has no clock.
        inline bool char_ticks(scc_channel_t channel, bool is_tx, uint64_t &ticks) {
            uint64_t hz = 0, div = 1;
            if (!clock_source(channel, is_tx, hz, div) || hz == 0) return false;
            const uint64_t bits = get_bits_per_char(channel, is_tx);
            const uint64_t mode = clock_mode_table[registers[channel].r4_clock_mode];
            // MASTER_CLOCK * bits * mode * div fits comfortably: 14.3e6 * 12 * 64 * 131074 < 2^64
            ticks = (MASTER_CLOCK * bits * mode * div) / hz;
            if (ticks == 0) ticks = 1;
            return true;
        }

        inline void update_timing_sources(scc_channel_t channel) {
            uint16_t clock_mode_index = registers[channel].r4_clock_mode;
            uint16_t new_mode = clock_mode_table[clock_mode_index];
            clock_mode[channel] = new_mode;
            uint64_t hz = 0, div = 1;
            const bool clocked = clock_source(channel, true, hz, div);
            this->baud_rate[channel] = (clocked && hz) ? (float)((double)hz / ((double)new_mode * (double)div)) : 0.0f;
            if (SCDEBUG) printf("SCC: Ch %d: Clock Mode: %d, Baud Rate: %08.2f\n", channel, new_mode, baud_rate[channel]);
            push_line_params(channel);
            // A character that had no clock starts its time now that it may have one.
            if (registers[channel].tx_in_progress && tx_stalled[channel]) schedule_tx_complete(channel);
            if (registers[channel].rx_in_progress && rx_stalled[channel]) schedule_rx_complete(channel);
            zc_arm(channel);
        }

        void schedule_tx_complete(scc_channel_t channel) {
            uint64_t ticks = 0;
            if (!event_timer || !clock) { tx_stalled[channel] = false; tx_complete(channel); return; }
            if (!char_ticks(channel, true, ticks)) { tx_stalled[channel] = true; return; }
            tx_stalled[channel] = false;
            tx_due_14m[channel] = clock->get_c14m() + ticks;
            event_timer->scheduleEvent(tx_due_14m[channel], tx_complete_callback, tx_timer_id[channel], this);
        }

        void schedule_rx_complete(scc_channel_t channel) {
            uint64_t ticks = 0;
            if (!event_timer || !clock) { rx_stalled[channel] = false; rx_complete(channel); return; }
            if (!char_ticks(channel, false, ticks)) { rx_stalled[channel] = true; return; }
            rx_stalled[channel] = false;
            rx_due_14m[channel] = clock->get_c14m() + ticks;
            event_timer->scheduleEvent(rx_due_14m[channel], rx_complete_callback, rx_timer_id[channel], this);
        }

        // ---- External/Status -------------------------------------------------------
        inline uint8_t ext_live_bits(scc_channel_t channel) const {
            return (uint8_t)((zero_count[channel] ? 0x02 : 0) | (dcd_in[channel] ? 0x08 : 0) |
                             (cts_in[channel] ? 0x20 : 0) | (break_in[channel] ? 0x80 : 0));
        }
        inline uint8_t ext_ie_mask(scc_channel_t channel) const {
            return (uint8_t)(registers[channel].w_regs[WR15] & 0xAA);
        }
        // A source may have changed: latch and interrupt if an enabled one did.
        void ext_update(scc_channel_t channel) {
            const uint8_t live = ext_live_bits(channel);
            if (!ext_latched[channel]) {
                if ((live ^ ext_last_bits[channel]) & ext_ie_mask(channel)) {
                    ext_latched[channel] = true;
                    ext_latched_bits[channel] = live;
                    if (registers[channel].r1_ext_int_enable) ext_ip_latch[channel] = true;
                }
                ext_last_bits[channel] = live;
            }
            // Auto enables: CTS gates the transmitter; a character waiting for it may now go.
            if (registers[channel].r3_auto_enables && cts_in[channel] && !registers[channel].tx_in_progress &&
                !registers[channel].r0_tx_buffer_empty) tx_load_shift(channel);
            update_interrupts(channel);
        }
        void ext_reset(scc_channel_t channel) {
            ext_ip_latch[channel] = false;
            const bool was = ext_latched[channel];
            const uint8_t held = ext_latched_bits[channel];
            ext_latched[channel] = false;
            const uint8_t live = ext_live_bits(channel);
            if (was && ((live ^ held) & ext_ie_mask(channel))) {   // moved while latched: interrupt again
                ext_latched[channel] = true;
                ext_latched_bits[channel] = live;
                if (registers[channel].r1_ext_int_enable) ext_ip_latch[channel] = true;
            }
            ext_last_bits[channel] = live;
        }

        // ---- zero count: an Ext/Status source at the BRG's count-zero rate, armed only while
        // WR15 enables it and the BRG runs (it would otherwise be tens of thousands of events a
        // second that nothing asked for).
        void zc_arm(scc_channel_t channel) {
            const auto &r = registers[channel];
            if (!event_timer || !clock || zc_scheduled[channel]) return;
            if (!(r.w_regs[WR15] & 0x02) || !r.r14_br_gen_enable) return;
            const uint64_t hz = r.r14_br_gen_source ? SCC_PCLK_HZ : SCC_RX_CLOCK;
            uint64_t period = (MASTER_CLOCK * ((uint64_t)r.r12_time_constant + 2ull)) / hz;
            if (period == 0) period = 1;
            zc_scheduled[channel] = true;
            event_timer->scheduleEvent(clock->get_c14m() + period, zc_callback, zc_timer_id[channel], this);
        }
        void zc_tick(scc_channel_t channel) {
            zc_scheduled[channel] = false;
            zero_count[channel] = true;       // the counter is at zero for this instant
            ext_update(channel);
            zero_count[channel] = false;
            ext_last_bits[channel] = ext_latched[channel] ? ext_last_bits[channel] : ext_live_bits(channel);
            zc_arm(channel);
        }
        static void zc_callback(uint64_t instanceID, void *userData) {
            Z85C30 *scc = static_cast<Z85C30 *>(userData);
            if (instanceID == scc->zc_timer_id[SCC_CHANNEL_A]) scc->zc_tick(SCC_CHANNEL_A);
            else if (instanceID == scc->zc_timer_id[SCC_CHANNEL_B]) scc->zc_tick(SCC_CHANNEL_B);
        }

        // ---- lines out ----------------------------------------------------------------
        void lines_out_update(scc_channel_t channel, bool force = false) {
            if (serial_devices[channel] == nullptr) return;
            const auto &r = registers[channel];
            const uint8_t ctl = (uint8_t)((r.r5_dtr ? 1 : 0) | (r.r5_rts ? 2 : 0));
            if (force || ctl != line_ctl_last[channel]) {
                if (serial_devices[channel]->q_host.send(SerialMessage{MESSAGE_MODEM_CTL, ctl})) line_ctl_last[channel] = ctl;
            }
            const uint8_t brk = r.r5_send_break ? 1 : 0;
            if (force || brk != break_out_last[channel]) {
                if (serial_devices[channel]->q_host.send(SerialMessage{brk ? MESSAGE_BREAK_ON : MESSAGE_BREAK_OFF, 0}))
                    break_out_last[channel] = brk;
            }
        }

        inline void push_line_params(scc_channel_t channel) {
            if (serial_devices[channel] == nullptr) {
                return;
            }
            host_serial_line_t line;
            float br = baud_rate[channel];
            line.baud = (br <= 0.0f) ? 9600 : static_cast<uint32_t>(br + 0.5f);

            switch (registers[channel].r5_tx_bits_per_char) {
                case 0b00: line.data_bits = 5; break;
                case 0b01: line.data_bits = 7; break;
                case 0b10: line.data_bits = 6; break;
                default:   line.data_bits = 8; break;
            }

            if (!registers[channel].r4_parity_enable) {
                line.parity = HOST_SERIAL_PARITY_NONE;
            } else if (registers[channel].r4_parity_even_odd) {
                line.parity = HOST_SERIAL_PARITY_EVEN;
            } else {
                line.parity = HOST_SERIAL_PARITY_ODD;
            }

            switch (registers[channel].r4_stop_bits) {
                case 0b10: line.stop_bits = 15; break;
                case 0b11: line.stop_bits = 2; break;
                default:   line.stop_bits = 1; break;
            }

            // FORK: only when something changed (every WR3/WR5 write used to re-send it).
            const uint64_t packed = host_serial_pack_line(line);
            if (packed == line_params_last[channel]) return;
            if (serial_devices[channel]->q_host.send(SerialMessage{MESSAGE_LINE, packed})) line_params_last[channel] = packed;
        }

        static inline uint8_t ip_shift(scc_channel_t channel) { return channel == SCC_CHANNEL_A ? 3 : 0; }
        static inline int highest_bit(uint8_t v) {
            for (int b = 5; b >= 0; b--) if (v & (1u << b)) return b;
            return -1;
        }

        // Special receive condition: overrun, framing, or parity when WR1 D2 makes it one.
        inline bool rx_special(scc_channel_t channel) {
            const auto &r = registers[channel];
            return r.r1_rx_overrun_err || r.r1_crc_framing_err || (r.r1_parity_err && r.r1_partity_spcond);
        }

        // "If the IE bit is not set ... the IP for that source is never set."
        inline void recompute_ip() {
            uint8_t ip = 0;
            for (int c = 0; c < SCC_CHANNEL_COUNT; c++) {
                const scc_channel_t ch = (scc_channel_t)c;
                const auto &r = registers[ch];
                const uint8_t sh = ip_shift(ch);
                const uint8_t mode = r.r1_rx_int_mode;   // 00 off, 01 first char or special, 10 all chars or special, 11 special only
                bool rx = false;
                if (mode != 0b00) {
                    if (rx_special(ch)) rx = true;
                    else if (r.r0_rx_char_available && (mode == 0b10 || (mode == 0b01 && rx_first_armed[ch]))) rx = true;
                }
                if (rx) ip |= (uint8_t)(0x04 << sh);
                if (r.r1_tx_int_enable && tx_ip_latch[ch]) ip |= (uint8_t)(0x02 << sh);
                if (r.r1_ext_int_enable && ext_ip_latch[ch]) ip |= (uint8_t)(0x01 << sh);
            }
            ip_bits = ip;
            registers[SCC_CHANNEL_A].r_reg_3 = ip;
            registers[SCC_CHANNEL_B].r_reg_3 = 0;
        }

        // RR2B: WR2 modified by the highest-priority pending source (datasheet codes,
        // V3-V1): B Tx 000, B Ext 001, B Rx 010, B special 011, A Tx 100, A Ext 101,
        // A Rx 110, A special 111; nothing pending reads 011.
        inline uint8_t calculate_modified_vector(scc_channel_t /*channel*/) {
            uint8_t vector = registers[SCC_CHANNEL_A].w_regs[WR2];
            uint8_t code = 0b011;
            const int hb = highest_bit(ip_bits);
            if (hb >= 0) {
                const bool chan_a = hb >= 3;
                const int src = hb - (chan_a ? 3 : 0);   // 2 Rx, 1 Tx, 0 Ext
                const scc_channel_t ch = chan_a ? SCC_CHANNEL_A : SCC_CHANNEL_B;
                uint8_t c = 0b01;
                if (src == 2) c = rx_special(ch) ? 0b11 : 0b10;
                else if (src == 1) c = 0b00;
                code = (uint8_t)((chan_a ? 0b100 : 0) | c);
            }
            if (registers[SCC_CHANNEL_A].r9_status_high_low) {
                // Status High: V4-V6, bit order reversed.
                const uint8_t rev = (uint8_t)(((code & 0b001) << 2) | (code & 0b010) | ((code & 0b100) >> 2));
                vector = (uint8_t)((vector & 0b1000'1111) | (rev << 4));
            } else {
                // Status Low: V1-V3. (The mask kept ONLY bits 3-1 before, zeroing the rest of WR2.)
                vector = (uint8_t)((vector & 0b1111'0001) | (code << 1));
            }
            return vector;
        }

        // INT: MIE and a pending source of higher priority than every source under service.
        // Computed across both channels on every change.
        inline void update_interrupts(scc_channel_t /*channel*/ = SCC_CHANNEL_A) {
            recompute_ip();
            const int hp = highest_bit(ip_bits);
            const int hs = highest_bit(ius_bits);
            const bool assert_int = registers[SCC_CHANNEL_A].r9_mie && hp >= 0 && hp > hs;
            irq_control->set_irq(IRQ_ID_SCC, assert_int);
        }

        inline void print_read_register(scc_channel_t channel, uint8_t reg_num, uint8_t retval) {
            if (SCDEBUG) printf("SCC: READ  %c :: %2d <== %02X\n", ch_name(channel), reg_num, retval);
        }

        inline void print_write_register(scc_channel_t channel, uint8_t reg_num, uint8_t data) {
            if (SCDEBUG) printf("SCC: WRITE %c :: %2d ==> %02X\n", ch_name(channel), reg_num, data);
        }


        /* Writes */

        inline void write_register_0(scc_channel_t channel, uint8_t data) {
            print_write_register(channel, WR0, data);
            {
                registers[channel].w_regs[WR0] = data;
                uint16_t new_register = registers[channel].r0_reg_num;
                // TODO: need to process the various commands here. (resetting interrupts, etc.)
                switch (registers[channel].r0_cmd) {
                    case 0b001: new_register += 8; break;                          // point high
                    case 0b010: ext_reset(channel); break;                         // reset Ext/Status interrupts (this channel)
                    case 0b011: break;                                             // send abort (SDLC)
                    case 0b100: rx_first_armed[channel] = true; break;             // enable int on next Rx character
                    case 0b101: tx_ip_latch[channel] = false; break;               // reset Tx int pending
                    case 0b110:                                                    // error reset
                        registers[channel].r1_parity_err = 0;
                        registers[channel].r1_rx_overrun_err = 0;
                        registers[channel].r1_crc_framing_err = 0;
                        registers[channel].r1_end_of_frame = 0;
                        break;
                    case 0b111: {                                                  // reset highest IUS
                        const int hs = highest_bit(ius_bits);
                        if (hs >= 0) ius_bits &= (uint8_t)~(1u << hs);
                        break;
                    }
                    default: break;
                }

                if (registers[channel].r0_reset_cmd == 0b01) { // reset Rx CRC Checker
                    // this is for SDLC etc mode
                } else if (registers[channel].r0_reset_cmd == 0b10) { // reset Tx CRC Generator
                    // this is for SDLC etc mode
                } else if (registers[channel].r0_reset_cmd == 0b11) { // reset Tx Underrun/EOM Latch
                    registers[channel].r0_tx_underrun_eom = 0; // ??? and we never set it anywhere. that's fine.
                }

                /* else {
                    printf("SCC: Unimplemented WRITE %c r0_reset_cmd command: Reg %d = %02X\n", ch_name(channel), new_register, registers[channel].r0_reset_cmd);
                } */
                reg_select[channel] = new_register;
                if (SCDEBUG) printf("SCC: Register Select %c -> New Reg %d\n", ch_name(channel), new_register);
            }
            update_interrupts(channel); // might have changed interrupt status
        }

        inline void write_register_1(scc_channel_t channel, uint8_t data) {
            print_write_register(channel, WR1, data);
            registers[channel].w_regs[WR1] = data;
            if (!registers[channel].r1_tx_int_enable) tx_ip_latch[channel] = false;   // IE clear: the IP is never set
            update_interrupts(channel); // might have changed interrupt enables
        }

        // partially shared between channels. Write: identical. Read: different
        inline void write_register_2(uint8_t data) {
            print_write_register(SCC_CHANNEL_A, WR2, data); // we store this in Ch A
            registers[SCC_CHANNEL_A].w_regs[WR2] = data;
        }

        inline void write_register_3(scc_channel_t channel, uint8_t data) {
            print_write_register(channel, WR3, data);
            registers[channel].w_regs[WR3] = data;
            push_line_params(channel);
        }

        inline void write_register_4(scc_channel_t channel, uint8_t data) {
            print_write_register(channel, WR4, data);
            registers[channel].w_regs[WR4] = data;
            update_timing_sources(channel);
        }

        inline void write_register_5(scc_channel_t channel, uint8_t data) {
            print_write_register(channel, WR5, data);
            registers[channel].w_regs[WR5] = data;
            push_line_params(channel);
            lines_out_update(channel);   // FORK: DTR, RTS and break reach the device
            // FORK: a character waiting in the buffer starts once the transmitter is enabled.
            if (registers[channel].r5_tx_enable && !registers[channel].tx_in_progress &&
                !registers[channel].r0_tx_buffer_empty) tx_load_shift(channel);
        }

        inline void write_register_6(scc_channel_t channel, uint8_t data) {
            print_write_register(channel, WR6, data);
            registers[channel].w_regs[WR6] = data;
        }

        inline void write_register_7(scc_channel_t channel, uint8_t data) {
            print_write_register(channel, WR7, data);
            registers[channel].w_regs[WR7] = data;
        }

        inline void write_register_9(uint8_t data) {
            print_write_register(SCC_CHANNEL_A, WR9, data);

            // store new value first. "bits in wr9 may be written at same time as the reset command
            // because these bits are affected only by a hardware reset. (pg 3-8)"
            registers[SCC_CHANNEL_A].w_regs[WR9] = data;
            uint8_t reset_cmd = registers[SCC_CHANNEL_A].r9_reset;
            if (reset_cmd == 0x01) { // Channel Reset B
                soft_reset_channel(SCC_CHANNEL_B); // software reset for Ch B
            }
            if (reset_cmd == 0x02) { // Channel Reset A
                soft_reset_channel(SCC_CHANNEL_A);
            }
            if (reset_cmd == 0x03) { // Force HW Reset - same as external chip reset (pg 3-8)
                reset();
            }
        }

        inline void write_register_10(scc_channel_t channel, uint8_t data) {
            print_write_register(channel, WR10, data);
            registers[channel].w_regs[WR10] = data;
        }

        inline void write_register_11(scc_channel_t channel, uint8_t data) {
            print_write_register(channel, WR11, data);
            registers[channel].w_regs[WR11] = data;
            update_timing_sources(channel);
        }
        inline void write_register_12(scc_channel_t channel, uint8_t data) {
            print_write_register(channel, WR12, data);
            registers[channel].w_regs[WR12] = data;
            update_timing_sources(channel);
        }
        inline void write_register_13(scc_channel_t channel, uint8_t data) {
            print_write_register(channel, WR13, data);
            registers[channel].w_regs[WR13] = data;
            update_timing_sources(channel);
        }
        inline void write_register_14(scc_channel_t channel, uint8_t data) {
            print_write_register(channel, WR14, data);
            registers[channel].w_regs[WR14] = data;
            update_timing_sources(channel);   // FORK: WR14 enables the BRG and picks its source
        }
        inline void write_register_15(scc_channel_t channel, uint8_t data) {
            print_write_register(channel, WR15, data);
            registers[channel].w_regs[WR15] = data;
            ext_last_bits[channel] = ext_live_bits(channel);   // FORK: enabling a source is not a change of it
            zc_arm(channel);
        }

        /* Reads */
        inline uint8_t read_register_0(scc_channel_t channel) {
            //printf("SCC: READ  register 0: Ch %d\n", channel);
            //registers[channel].r0_tx_buffer_empty = 1; // duh, don't override this.
            // FORK: a read no longer writes DCD and CTS; they are set at construction and
            // left alone (external pins, not modelled yet -- the study's step 4).
            // FORK: D1/D3/D5/D7 are the Ext/Status sources -- as latched when an enabled one
            // changed, otherwise live; D0, D2, D4 and D6 are the chip's own.
            const uint8_t ext = ext_latched[channel] ? ext_latched_bits[channel] : ext_live_bits(channel);
            uint8_t retval = (uint8_t)((registers[channel].r_reg_0 & 0x55) | (ext & 0xAA));
            print_read_register(channel, RR0, retval);
            return retval; // TODO: fix. for now fake CTS=1, DCD=1, Tx Buffer Empty=1
        }

        inline uint8_t read_register_1(scc_channel_t channel) {
            // FORK: All Sent is the shift register, not a copy of Tx Buffer Empty.
            registers[channel].r1_all_sent = !registers[channel].tx_in_progress && registers[channel].r0_tx_buffer_empty;
            uint8_t retval = registers[channel].r_reg_1;
            print_read_register(channel, RR1, retval);
            return retval; // TODO: this is reasonable, we won't have errors.. what is residue code?
        }
        inline uint8_t read_register_2(scc_channel_t channel) {
            // Channel A: return the interrupt vector exactly as-is.
            // Channel B: return the "modified interrupt vector".
            uint8_t retval;
            if (channel == SCC_CHANNEL_B) {
                 // when vector is read from Channel B, it always includes the status regardless of the VIS bit
                 // the status given will decode the highest priority interrupt pending at the time it is read.
                retval = calculate_modified_vector(channel);
            } else {
                retval = registers[SCC_CHANNEL_A].w_regs[WR2];
            }
            // FORK: with software INTACK enabled (WR9 D5), reading RR2 is the acknowledge:
            // the highest pending source goes under service until Reset Highest IUS.
            if (registers[SCC_CHANNEL_A].r9_software_intack_enable) {
                recompute_ip();
                const int hp = highest_bit(ip_bits);
                if (hp >= 0 && hp > highest_bit(ius_bits)) ius_bits |= (uint8_t)(1u << hp);
            }
            print_read_register(channel, RR2, retval);
            return retval;
        }
        inline uint8_t read_register_3(scc_channel_t channel) {
            uint8_t retval = 0x00;
            if (channel == SCC_CHANNEL_A) {
                retval = registers[SCC_CHANNEL_A].r_reg_3;
            }
            print_read_register(channel, RR3, retval);
            return retval; // B is always 0
        }
        inline uint8_t read_register_10(scc_channel_t channel) {
            uint8_t retval = registers[channel].r_reg_10; // TODO: need to return the actual miscellaneous status;
            print_read_register(channel, RR10, retval);
            return retval;
        }
        inline uint8_t read_register_12(scc_channel_t channel) {
            uint8_t retval = registers[channel].w_regs[RR12];
            print_read_register(channel, RR12, retval);
            return retval;
        }
        inline uint8_t read_register_13(scc_channel_t channel) {
            uint8_t retval = registers[channel].w_regs[RR13];
            print_read_register(channel, RR13, retval);
            return retval;
        }
        inline uint8_t read_register_15(scc_channel_t channel) {
            uint8_t retval = registers[channel].w_regs[RR15] & 0b1111'1010; // two bits always 0
            print_read_register(channel, RR15, retval);
            return retval;
        }

    public:
        Z85C30(InterruptController *irq_control, EventTimer *event_timer = nullptr, NClockII *clock = nullptr, uint64_t base_instance_id = 0x5CC0000)
            : irq_control(irq_control), event_timer(event_timer), clock(clock) {
            // Set up unique instance IDs for each channel's TX and RX timers
            tx_timer_id[SCC_CHANNEL_A] = base_instance_id + 0;
            tx_timer_id[SCC_CHANNEL_B] = base_instance_id + 1;
            rx_timer_id[SCC_CHANNEL_A] = base_instance_id + 2;
            rx_timer_id[SCC_CHANNEL_B] = base_instance_id + 3;
            rx_poll_id[SCC_CHANNEL_A] = base_instance_id + 4;
            rx_poll_id[SCC_CHANNEL_B] = base_instance_id + 5;
            zc_timer_id[SCC_CHANNEL_A] = base_instance_id + 6;
            zc_timer_id[SCC_CHANNEL_B] = base_instance_id + 7;

            // clear the data structures on boot.
            memset(reg_select, 0, sizeof(reg_select));
            memset(registers, 0, sizeof(registers));
            registers[SCC_CHANNEL_A].r0_dcd = 1; registers[SCC_CHANNEL_A].r0_cts = 1;
            registers[SCC_CHANNEL_B].r0_dcd = 1; registers[SCC_CHANNEL_B].r0_cts = 1;
            reset();
        };

        ~Z85C30() {}

       /*  void set_data_file(scc_channel_t channel, FILE *data_file) {
            this->data_files[channel] = data_file;
        } */


        inline void set_bits_by_mask(uint8_t &reg, uint8_t mask, uint8_t value) {
            reg = (reg & ~mask) | (value & mask);
        }

        /* HW Reset according to Technical Manual
           use full set_bits_by_mask even if we're setting all bits, for consistency. The optimizer will probably optimize it out.
        */
        void hw_reset_channel(scc_channel_t channel) {
            tx_ip_latch[channel] = false;
            ext_ip_latch[channel] = false;
            rx_first_armed[channel] = true;
            ius_bits &= (uint8_t)~(0x07 << ip_shift(channel));
            // Cancel any pending TX/RX events
            if (event_timer) {
                event_timer->cancelEvents(tx_timer_id[channel]);
                event_timer->cancelEvents(rx_timer_id[channel]);
            }

            registers[channel].tx_in_progress = false;
            registers[channel].rx_in_progress = false;
            rx_fifo[channel].count = 0;   // FORK: RR0 D0 resets to 0 -- nothing is available
            tx_stalled[channel] = false; rx_stalled[channel] = false;   // FORK: clocks and Ext/Status
            ext_latched[channel] = false; zero_count[channel] = false;
            if (event_timer) event_timer->cancelEvents(zc_timer_id[channel]);
            zc_scheduled[channel] = false;
            set_bits_by_mask(registers[channel].w_regs[WR0], 0b1111'1111, 0b0000'0000);
            set_bits_by_mask(registers[channel].w_regs[WR1], 0b1101'1011, 0b0000'0000);
            set_bits_by_mask(registers[channel].w_regs[WR3], 0b0000'0001, 0b0000'0000);
            set_bits_by_mask(registers[channel].w_regs[WR4], 0b0000'0100, 0b000'0100);
            set_bits_by_mask(registers[channel].w_regs[WR5], 0b1001'1110, 0b0000'0000);
            // only one WR9 exists in the SCC and it can be accessed from either channel. (pg 7-10)
            set_bits_by_mask(registers[SCC_CHANNEL_A].w_regs[WR9], 0b1111'1100, 0b1100'0000);
            set_bits_by_mask(registers[channel].w_regs[WR10], 0b1111'1111, 0b0000'0000);
            set_bits_by_mask(registers[channel].w_regs[WR11], 0b1111'1111, 0b0000'1000);
            set_bits_by_mask(registers[channel].w_regs[WR14], 0b0011'1111, 0b0010'0000);
            set_bits_by_mask(registers[channel].w_regs[WR15], 0b1111'1111, 0b1111'1000);
            set_bits_by_mask(registers[channel].r_reg_0, 0b1100'0111, 0b0100'0100);
            set_bits_by_mask(registers[channel].r_reg_1, 0b1111'1111, 0b0000'0110);
            set_bits_by_mask(registers[channel].r_reg_3, 0b1111'1111, 0b0000'0000);
            set_bits_by_mask(registers[channel].r_reg_10, 0b1111'1111, 0b0000'0000);

            update_timing_sources(channel);
            update_interrupts(channel);
            // need to update interrupts
        }

        /* Channel Reset according to Technical Manual
            it's identical with 3 differences */
        void soft_reset_channel(scc_channel_t channel) {
            tx_ip_latch[channel] = false;
            ext_ip_latch[channel] = false;
            rx_first_armed[channel] = true;
            ius_bits &= (uint8_t)~(0x07 << ip_shift(channel));
            // Cancel any pending TX/RX events
            if (event_timer) {
                event_timer->cancelEvents(tx_timer_id[channel]);
                event_timer->cancelEvents(rx_timer_id[channel]);
            }

            registers[channel].tx_in_progress = false;
            registers[channel].rx_in_progress = false;
            rx_fifo[channel].count = 0;   // FORK: RR0 D0 resets to 0 -- nothing is available
            tx_stalled[channel] = false; rx_stalled[channel] = false;   // FORK: clocks and Ext/Status
            ext_latched[channel] = false; zero_count[channel] = false;
            if (event_timer) event_timer->cancelEvents(zc_timer_id[channel]);
            zc_scheduled[channel] = false;
            set_bits_by_mask(registers[channel].w_regs[WR0], 0b1111'1111, 0b0000'0000);
            set_bits_by_mask(registers[channel].w_regs[WR1], 0b1101'1011, 0b0000'0000);
            set_bits_by_mask(registers[channel].w_regs[WR3], 0b0000'0001, 0b0000'0000);
            set_bits_by_mask(registers[channel].w_regs[WR4], 0b0000'0100, 0b000'0100);
            set_bits_by_mask(registers[channel].w_regs[WR5], 0b1001'1110, 0b0000'0000);
            // only one WR9 exists in the SCC and it can be accessed from either channel. (pg 7-10)
            set_bits_by_mask(registers[SCC_CHANNEL_A].w_regs[WR9], 0b00100000, 0b0000'0000); // different than hw reset
            set_bits_by_mask(registers[channel].w_regs[WR10], 0b1001'1111, 0b0000'0000); // different than hw reset
            set_bits_by_mask(registers[channel].w_regs[WR11], 0b1111'1111, 0b0000'1000);
            set_bits_by_mask(registers[channel].w_regs[WR14], 0b0011'1100, 0b0010'0000); // different than hw reset
            set_bits_by_mask(registers[channel].w_regs[WR15], 0b1111'1111, 0b1111'1000);
            set_bits_by_mask(registers[channel].r_reg_0, 0b1100'0111, 0b0100'0100);
            set_bits_by_mask(registers[channel].r_reg_1, 0b1111'1111, 0b0000'0110);
            set_bits_by_mask(registers[channel].r_reg_3, 0b1111'1111, 0b0000'0000);
            set_bits_by_mask(registers[channel].r_reg_10, 0b1111'1111, 0b0000'0000);

            update_timing_sources(channel);
            update_interrupts(channel);
            // need to update interrupts
        }


        void reset() {
            // the channel reset and HW reset are not identical.
            hw_reset_channel(SCC_CHANNEL_A);
            hw_reset_channel(SCC_CHANNEL_B);
        }

        void writeCmd(scc_channel_t channel, uint8_t data) {
            // current reg_select for channel
            update_queues();

            uint16_t current_reg_select = reg_select[channel];
            switch (current_reg_select) {
                case WR0: write_register_0(channel, data); break;
                case WR1: write_register_1(channel, data);  reg_select[channel] = 0; break;
                case WR2: write_register_2(data);  reg_select[channel] = 0; break;
                case WR3: write_register_3(channel, data);  reg_select[channel] = 0; break;
                case WR4: write_register_4(channel, data);  reg_select[channel] = 0; break;
                case WR5: write_register_5(channel, data);  reg_select[channel] = 0; break;
                case WR6: write_register_6(channel, data);  reg_select[channel] = 0; break;
                case WR7: write_register_7(channel, data);  reg_select[channel] = 0; break;
                case WR8: writeData(channel, data);  reg_select[channel] = 0; break;
                case WR9: write_register_9(data);  reg_select[channel] = 0; break;
                case WR10: write_register_10(channel, data);  reg_select[channel] = 0; break;
                case WR11: write_register_11(channel, data);  reg_select[channel] = 0; break;
                case WR12: write_register_12(channel, data);  reg_select[channel] = 0; break;
                case WR13: write_register_13(channel, data);  reg_select[channel] = 0; break;
                case WR14: write_register_14(channel, data);  reg_select[channel] = 0; break;
                case WR15: write_register_15(channel, data);  reg_select[channel] = 0; break;
                default:
                    if (SCDEBUG) printf("SCC: Unimplemented WRITE %c register: Reg %d = %02X\n", ch_name(channel), current_reg_select, data);
                    reg_select[channel] = 0;
                    break;
            }
            // redundant: printf("SCC: WRITE register: Ch %d / Reg %d = %02X\n", channel, current_reg_select, data);
             // after any other write, reset the register select
        }

        /*
            if write is in progress and they wrote anyway, what do we do?
            if write not in progress,
                go ahead and chuck the character.
                schedule a timer based on the baud rate and the number of bits - char bits + start bits + stop bits.
                set a flag to indicate that write is in progress.
                when the timer is called, clear that flag.
        */
        void writeData(scc_channel_t channel, uint8_t data) {
            if (SCDEBUG) printf("SCC: WRITE %c DATA = %02X (tx_enable=%d)\n", ch_name(channel), data, registers[channel].r5_tx_enable);

            update_queues();

            // Check if TX is enabled
            if (!registers[channel].r5_tx_enable) {
                if (SCDEBUG) printf("SCC: Ch %c TX disabled, dropping char %02X\n", ch_name(channel), data);
                return;
            }

            // Writing a character resets the Tx IP (datasheet).
            tx_ip_latch[channel] = false;

            // FORK: into the transmit buffer. A character already waiting there (the shift
            // register busy) is overwritten, as a write to a full buffer is on the chip.
            if (!registers[channel].r0_tx_buffer_empty && SCDEBUG)
                printf("SCC: Ch %c TX buffer overwritten - %02X lost\n", ch_name(channel), registers[channel].char_tx);
            registers[channel].char_tx = data;
            registers[channel].r0_tx_buffer_empty = false;

            // An idle transmitter takes it straight into the shift register.
            if (!registers[channel].tx_in_progress) tx_load_shift(channel);

            update_interrupts(channel);
        }

        // FORK: transmit buffer -> shift register. The buffer is empty again, so Tx IP sets
        // (when enabled) and the guest may load the next character while this one is sent.
        void tx_load_shift(scc_channel_t channel) {
            auto &r = registers[channel];
            // FORK: with auto enables (WR3 D5) CTS is the transmitter enable; the character
            // waits in the buffer until CTS asserts (ext_update starts it).
            if (r.r3_auto_enables && !cts_in[channel]) return;
            tx_shift[channel] = r.char_tx;
            r.r0_tx_buffer_empty = true;
            r.tx_in_progress = true;
            if (r.r1_tx_int_enable) tx_ip_latch[channel] = true;
            schedule_tx_complete(channel);
        }

        uint8_t readCmd(scc_channel_t channel) {

            update_queues();

            uint8_t retval = 0x00;
            uint16_t current_reg_select = reg_select[channel];
            switch (current_reg_select) {
                case RR0: case RR4: retval = read_register_0(channel); break;
                case RR1: case RR5: retval = read_register_1(channel); break;
                case RR2: case RR6: retval = read_register_2(channel); break;
                case RR3: case RR7: retval = read_register_3(channel);  break;
                case RR8: retval = readData(channel); break;
                case RR10: case RR14:  retval = read_register_10(channel); break;
                case RR12: retval = read_register_12(channel); break;
                case RR9: case RR13: retval = read_register_13(channel); break;
                case RR11: case RR15: retval = read_register_15(channel); break;
                default:
                    if (SCDEBUG) printf("SCC: Unimplemented READ %c register: Reg %d\n", ch_name(channel), current_reg_select);
            }
            //printf("SCC: READ  %c register: Reg %d = %02X\n", ch_name(channel), current_reg_select, retval);
            reg_select[channel] = 0;
            update_interrupts(channel);
            return retval;
        }

        // this should be called before reads or writes; and also based on baud timer.
        // this used to rely on the IP flag but that's not right! now uses rx_char_available
        void update_queues() {
            for (int channel = 0; channel < SCC_CHANNEL_COUNT; channel++) {
                rx_try_start((scc_channel_t)channel);
            }
        }

        // FORK: start receiving the next host byte if the receive shift register is idle.
        // Not in local loopback: RxD is disconnected from the pin there, and the host's
        // bytes wait instead of landing in the FIFO beside the looped-back ones. With no event
        // timer (nothing paces the receiver) a byte waits for FIFO room.
        void rx_try_start(scc_channel_t channel) {
            if (serial_devices[channel] == nullptr) return;
            if (registers[channel].rx_in_progress || registers[channel].r14_local_loopback) return;
            const bool timed = event_timer && clock;
            if (!timed && rx_fifo[channel].count >= 3) return;
            // FORK: line changes from the device apply in order with its bytes; a data byte
            // starts a character and ends the look.
            for (;;) {
                SerialMessage msg = serial_devices[channel]->q_dev.get();
                if (msg.type == MESSAGE_NONE) return;
                if (msg.type == MESSAGE_DATA) { schedule_rx_char(channel, (uint8_t)msg.data); return; }
                if (msg.type == MESSAGE_MODEM_STATUS) {
                    dcd_in[channel] = (msg.data & 1) != 0;
                    cts_in[channel] = (msg.data & 2) != 0;
                    ext_update(channel);
                } else if (msg.type == MESSAGE_BREAK_ON || msg.type == MESSAGE_BREAK_OFF) {
                    const bool on = msg.type == MESSAGE_BREAK_ON;
                    if (break_in[channel] && !on && registers[channel].r3_rx_enable) {
                        // A break ends with a single null character in the receiver.
                        rx_push(channel, 0x00);
                    }
                    break_in[channel] = on;
                    ext_update(channel);
                } else if (SCDEBUG) {
                    printf("SCC: READ %c: got unexpected message type: %d\n", ch_name(channel), msg.type);
                }
            }
        }

        // FORK: a character leaves the receive shift register for the FIFO.
        void rx_push(scc_channel_t channel, uint8_t data) {
            auto &f = rx_fifo[channel];
            if (f.count < 3) {
                f.data[f.count++] = data;
            } else {
                f.data[2] = data;
                registers[channel].r1_rx_overrun_err = 1;
                if (SCDEBUG) printf("SCC: Ch %c RX overrun - FIFO full\n", ch_name(channel));
            }
            registers[channel].r0_rx_char_available = 1;
        }

        void rx_poll_arm(scc_channel_t channel) {
            if (!event_timer || !clock || serial_devices[channel] == nullptr || rx_poll_scheduled[channel]) return;
            rx_poll_scheduled[channel] = true;
            event_timer->scheduleEvent(clock->get_c14m() + RX_POLL_14M, rx_poll_callback, rx_poll_id[channel], this);
        }

        void rx_poll(scc_channel_t channel) {
            rx_poll_scheduled[channel] = false;
            if (serial_devices[channel] == nullptr) return;
            rx_try_start(channel);
            rx_poll_arm(channel);
        }

        uint8_t readData(scc_channel_t channel) {
            uint8_t retval = 0x00;

            update_queues();

            auto &f = rx_fifo[channel];
            if (f.count > 0) {
                // FORK: the oldest FIFO character; Rx Character Available stays set while
                // more are waiting.
                retval = f.data[0];
                f.data[0] = f.data[1];
                f.data[1] = f.data[2];
                f.count--;
                registers[channel].r0_rx_char_available = f.count > 0;
                if (registers[channel].r1_rx_int_mode == 0b01) rx_first_armed[channel] = false;

                // do this instead in update_interrupts()
                /* if (channel == SCC_CHANNEL_A) {
                    registers[SCC_CHANNEL_A].r3_a_rx_pending = 0;
                } else {
                    registers[SCC_CHANNEL_A].r3_b_rx_pending = 0;
                } */
            }

            if (SCDEBUG) printf("SCC: READ  %c DATA = %02X\n", ch_name(channel), retval);

            update_interrupts(channel);
            return retval;
        }

        void set_device_channel(scc_channel_t channel, SerialDevice *device) {
            serial_devices[channel] = device;
            line_params_last[channel] = ~0ull;
            push_line_params(channel);
            lines_out_update(channel, true);   // FORK: the new device learns DTR, RTS and break
            // FORK: a new device starts with DCD and CTS asserted until it reports otherwise.
            dcd_in[channel] = true; cts_in[channel] = true; break_in[channel] = false;
            ext_update(channel);
            rx_poll_arm(channel);   // FORK: the receiver takes host bytes without a guest access
        }

        void debug_output(DebugFormatter *df) {
            df->addLine("Reg Select A: %02X  B: %02X", reg_select[SCC_CHANNEL_A], reg_select[SCC_CHANNEL_B]);
            df->addLine("Clock Mode A: %dX   B: %dX", clock_mode[SCC_CHANNEL_A], clock_mode[SCC_CHANNEL_B]);
            df->addLine("Rx Data A: %02X  B: %02X", registers[SCC_CHANNEL_A].char_rx, registers[SCC_CHANNEL_B].char_rx);
            df->addLine("Tx Data A: %02X  B: %02X", registers[SCC_CHANNEL_A].char_tx, registers[SCC_CHANNEL_B].char_tx);
            for (int c = 0; c < SCC_CHANNEL_COUNT; c++) {
                const rx_fifo_t &f = rx_fifo[c];
                df->addLine("%c Rx FIFO %d: %02X %02X %02X  shift %s  | Tx buffer %s  shift %s %02X",
                    ch_name(c), f.count, f.data[0], f.data[1], f.data[2],
                    registers[c].rx_in_progress ? "busy" : "idle",
                    registers[c].r0_tx_buffer_empty ? "empty" : "full",
                    registers[c].tx_in_progress ? "busy" : "idle", tx_shift[c]);
            }
            df->addLine("r_reg_0 A: %02X  B: %02X", registers[SCC_CHANNEL_A].r_reg_0, registers[SCC_CHANNEL_B].r_reg_0);
            df->addLine("r_reg_1 A: %02X  B: %02X", registers[SCC_CHANNEL_A].r_reg_1, registers[SCC_CHANNEL_B].r_reg_1);
            //df->addLine("r_reg_2 A: %02X  B: %02X", registers[SCC_CHANNEL_A].r_reg_2, registers[SCC_CHANNEL_B].r_reg_2);
            df->addLine("r_reg_3 A: %02X  B: 00", registers[SCC_CHANNEL_A].r_reg_3) ;
            df->addLine("r_reg_10 A: %02X  B: %02X", registers[SCC_CHANNEL_A].r_reg_10, registers[SCC_CHANNEL_B].r_reg_10);
            df->addLine("Baud Rate A: %08.2f  B: %08.2f", baud_rate[SCC_CHANNEL_A], baud_rate[SCC_CHANNEL_B]);
            for (int c = 0; c < SCC_CHANNEL_COUNT; c++) {
                const scc_channel_t ch = (scc_channel_t)c;
                df->addLine("%c lines in: DCD %d CTS %d break %d | out: DTR %d RTS %d break %d | ext %s %02X | Tx %s Rx %s",
                    ch_name(c), dcd_in[c], cts_in[c], break_in[c], registers[c].r5_dtr, registers[c].r5_rts,
                    registers[c].r5_send_break, ext_latched[c] ? "latched" : "live",
                    ext_latched[c] ? ext_latched_bits[c] : ext_live_bits(ch),
                    tx_stalled[c] ? "STALLED(no clock)" : "clocked", rx_stalled[c] ? "STALLED(no clock)" : "clocked");
            }
            for (int i = 0; i < 16; i++) {
                df->addLine("WReg[%2d]   A: %02X  B: %02X", i, registers[SCC_CHANNEL_A].w_regs[i], registers[SCC_CHANNEL_B].w_regs[i]);
            }
        }

        // TX completion - called when character transmission is complete
        void tx_complete(scc_channel_t channel) {
            if (SCDEBUG) printf("SCC: Ch %c TX complete, char=%02X, loopback=%d\n",
                ch_name(channel), registers[channel].char_tx, registers[channel].r14_local_loopback);

            registers[channel].tx_in_progress = false;

            // Now actually send the byte to the device (FORK: the shift register's byte; the
            // buffer and Tx IP were settled when it was loaded)
            uint8_t data = tx_shift[channel];

            if (registers[channel].r14_local_loopback) {
                // Local loopback - in loopback mode, RX happens instantaneously
                // The character doesn't actually go out on the wire, it's looped back internally
                if (SCDEBUG) printf("SCC: Ch %c looping back %02X (instant)\n", ch_name(channel), data);

                // Check if RX is enabled
                if (registers[channel].r3_rx_enable) {
                    // The character has already taken its time on the wire; into the FIFO.
                    registers[channel].char_rx = data;
                    rx_push(channel, data);
                    if (SCDEBUG) printf("SCC: Ch %c loopback RX complete (instant): %02X\n", ch_name(channel), data);
                } else {
                    if (SCDEBUG) printf("SCC: Ch %c RX disabled, dropping loopback char\n", ch_name(channel));
                }
            } else if (serial_devices[channel] != nullptr) {
                serial_devices[channel]->q_host.send(SerialMessage{MESSAGE_DATA, data});
            }

            // FORK: a character waiting in the buffer moves into the now-idle shift register.
            if (!registers[channel].r0_tx_buffer_empty) tx_load_shift(channel);

            update_interrupts(channel);
        }

        // RX completion - called when character reception is complete
        void rx_complete(scc_channel_t channel) {
            if (SCDEBUG) printf("SCC: Ch %c RX complete: %02X\n", ch_name(channel), registers[channel].char_rx);

            registers[channel].rx_in_progress = false;
            rx_push(channel, registers[channel].char_rx);   // FORK: into the FIFO

            update_interrupts(channel);
            rx_try_start(channel);   // FORK: back to back, at the line rate
        }

        // Schedule reception of a character
        void schedule_rx_char(scc_channel_t channel, uint8_t data) {
            // Check if RX is enabled
            if (!registers[channel].r3_rx_enable || (registers[channel].r3_auto_enables && !dcd_in[channel])) {
                // FORK: with auto enables (WR3 D5) DCD is the receiver enable.
                if (SCDEBUG) printf("SCC: Ch %c RX disabled, dropping char %02X\n", ch_name(channel), data);
                return;
            }

            // FORK: an overrun is decided when the character reaches the FIFO (rx_push).

            if (SCDEBUG) printf("SCC: Ch %c scheduling RX of %02X\n", ch_name(channel), data);

            registers[channel].char_rx = data;
            registers[channel].rx_in_progress = true;

            schedule_rx_complete(channel);   // FORK: clocked as WR11/WR14 route it (or stalled)
        }

        // Static callback wrappers for EventTimer
        static void tx_complete_callback(uint64_t instanceID, void* userData) {
            Z85C30* scc = static_cast<Z85C30*>(userData);
            // Determine channel from instance ID
            if (instanceID == scc->tx_timer_id[SCC_CHANNEL_A]) {
                scc->tx_complete(SCC_CHANNEL_A);
            } else if (instanceID == scc->tx_timer_id[SCC_CHANNEL_B]) {
                scc->tx_complete(SCC_CHANNEL_B);
            }
        }

        static void rx_poll_callback(uint64_t instanceID, void* userData) {
            Z85C30* scc = static_cast<Z85C30*>(userData);
            if (instanceID == scc->rx_poll_id[SCC_CHANNEL_A]) scc->rx_poll(SCC_CHANNEL_A);
            else if (instanceID == scc->rx_poll_id[SCC_CHANNEL_B]) scc->rx_poll(SCC_CHANNEL_B);
        }

        static void rx_complete_callback(uint64_t instanceID, void* userData) {
            Z85C30* scc = static_cast<Z85C30*>(userData);
            // Determine channel from instance ID
            if (instanceID == scc->rx_timer_id[SCC_CHANNEL_A]) {
                scc->rx_complete(SCC_CHANNEL_A);
            } else if (instanceID == scc->rx_timer_id[SCC_CHANNEL_B]) {
                scc->rx_complete(SCC_CHANNEL_B);
            }
        }

};
