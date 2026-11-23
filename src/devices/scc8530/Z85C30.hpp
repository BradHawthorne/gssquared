#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cassert>
#include "util/DebugFormatter.hpp"
#include "util/InterruptController.hpp"
#include "util/EventTimer.hpp"
#include "NClock.hpp"
#include "serial_devices/SerialDevice.hpp"
#include "a2gspu_scc_capture.hpp"
#include "serial_devices/host/HostSerial.hpp"
#include "iigs_line_activity.hpp"
#include "ltoudp.hpp"
#include <vector>
#include <cstdarg>
#include <cstdlib>
#include <chrono>

// Defined in the emulator (cpu.cpp). Weak so sccltoudptest can link without a CPU.
extern "C" uint32_t z85c30_query_guest_pc(void) __attribute__((weak));
static inline uint32_t z85c30_query_guest_pc_safe(void) {
    return z85c30_query_guest_pc ? z85c30_query_guest_pc() : 0u;
}

extern "C" uint8_t z85c30_query_guest_p(void) __attribute__((weak));
static inline uint8_t z85c30_query_guest_p_safe(void) {
    return z85c30_query_guest_p ? z85c30_query_guest_p() : 0u;
}
// Gated guest-memory peek (A2GSPU_LTOUDP_TRACE). probe_peek: no cycles.
extern "C" int z85c30_query_guest_peek(uint32_t addr, uint8_t *dst, size_t n)
    __attribute__((weak));
static inline int z85c30_query_guest_peek_safe(uint32_t addr, uint8_t *dst, size_t n) {
    return z85c30_query_guest_peek ? z85c30_query_guest_peek(addr, dst, n) : -1;
}

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
        struct rx_fifo_t { uint8_t data[3]; uint8_t eof[3]; uint8_t count; };
        rx_fifo_t rx_fifo[SCC_CHANNEL_COUNT] = {};
        // FORK: layer-2 LToUDP. sdlc_tx accumulates guest data bytes; rx_pending
        // feeds the 3-byte FIFO so a long LLAP frame does not overrun it.
        std::vector<uint8_t> sdlc_tx[SCC_CHANNEL_COUNT];
        struct rx_pend_t { uint8_t data; uint8_t eof; };
        std::vector<rx_pend_t> rx_pending[SCC_CHANNEL_COUNT];
        // Complete LLAP frames waiting to go onto the virtual LocalTalk
        // wire. The physical line is half-duplex: one SDLC frame at a time.
        // UDP can deliver a burst; the inbox holds later frames until
        // ltoudp_rx_ready (pending empty, FIFO empty or leftover EOF CRC).
        // This serializes the cable; it does not replace RTS/CTS (the sender
        // still waits for CTS before putting a data frame on the line).
        struct ltoudp_in_t {
            std::vector<uint8_t> llap;
            uint64_t due_14m = 0;
            bool has_due = false;
            ltoudp_in_t() = default;
            ltoudp_in_t(const uint8_t *a, const uint8_t *b) : llap(a, b) {}
            size_t size() const { return llap.size(); }
            uint8_t operator[](size_t i) const { return llap[i]; }
            const uint8_t *data() const { return llap.data(); }
        };
        std::vector<ltoudp_in_t> ltoudp_inbox;
        // Guest-time cable (Docs/LToUDP.md): solicited replies are collected
        // with the guest frozen, then delivered at TX EOF + IFG (~200 us).
        static constexpr uint64_t LTOUDP_IFG_14M = (MASTER_CLOCK * 200ull + 500000ull) / 1000000ull; // 2864
        // Inter-dialog gap: 400 us, 5727 C14M ticks. A new dialog (RTS/ENQ)
        // waits this long after the previous frame, not just one IFG.
        static constexpr uint64_t LTOUDP_IDG_14M = (MASTER_CLOCK * 400ull + 500000ull) / 1000000ull; // 5727
        // LocalTalk 230.4 kbps FM0, 8 data bits/character (~34.7 us, 497 C14M ticks).
        static constexpr uint64_t LTOUDP_BITRATE = 230400ull;
        static constexpr uint64_t LTOUDP_BYTE_14M =
            (MASTER_CLOCK * 8ull + LTOUDP_BITRATE / 2ull) / LTOUDP_BITRATE; // 497
        static constexpr int LTOUDP_REPLY_MS = 5;          // plain-peer host collect
        static constexpr int LTOUDP_LTG1_SAFETY_MS = 2000;  // logged as peer-fault, not a collision
        const char *ltoudp_last_collect_reason = "none";
        int64_t ltoudp_last_reply_host_us = -1;
        bool ltoudp_after_data = false;
        uint64_t ltoudp_test_now = 0;
        uint64_t ltoudp_tx_eof_14m = 0;
        bool ltoudp_after_cts = false;
        bool ltoudp_tifgap_open = false;
        bool ltoudp_atp1_bank_known = false;
        uint32_t ltoudp_atp1_bank = 0;
        const char *ltoudp_atp1_reg = nullptr;
        uint64_t ltoudp_atp1_reg_14m = 0;
        uint32_t ltoudp_atp1_reg_pc = 0;
          // used when no NClockII (host tests)
        uint64_t ltoudp_ifg_id = 0;
        bool ltoudp_ifg_scheduled = false;
        bool ltoudp_peer_ltg1 = false;
        inline static Z85C30 *ltoudp_hook_scc = nullptr;
        bool ltoudp_lockstep = false;
        bool ltoudp_in_horizon_wait = false;
        uint64_t ltoudp_grant_14m = ~uint64_t(0);
        uint64_t ltoudp_clamp_14m = 0;      // post-TX clamp; 0 = none
        uint64_t ltoudp_horizon_faults = 0;
        uint64_t ltoudp_beacon_id = 0;
        bool ltoudp_beacon_scheduled = false;
        uint64_t ltoudp_last_beacon_14m = 0;
        uint64_t ltoudp_rx_eof_14m = 0;
        static constexpr uint64_t LTOUDP_BEACON_14M = MASTER_CLOCK / 1000; // 1 ms coarse
        static constexpr uint64_t LTOUDP_TIFGAP1_14M = (MASTER_CLOCK * 448ull + 500000ull) / 1000000ull;
        // Byte-paced LToUDP RX (Z8530: wire -> shift register -> 3-byte FIFO).
        uint64_t ltoudp_rx_byte_id[SCC_CHANNEL_COUNT] = { 0, 0 };
        bool ltoudp_rx_byte_scheduled[SCC_CHANNEL_COUNT] = { false, false };
        uint64_t ltoudp_rx_due_14m[SCC_CHANNEL_COUNT] = { 0, 0 };
        size_t ltoudp_rx_frame_n[SCC_CHANNEL_COUNT] = { 0, 0 };
        size_t ltoudp_rx_frame_i[SCC_CHANNEL_COUNT] = { 0, 0 };
        struct ltoudp_due_t { std::vector<uint8_t> llap; uint64_t due_14m; };
        std::vector<ltoudp_due_t> ltoudp_due;
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

            // Stop bits (1, 1.5, or 2). FORK: WR4 bits 3-2 = 00 is sync/SDLC; those
            // frames have no async start/stop bits (Z8530 WR4, Sync Modes Enable).
            if (registers[channel].r4_stop_bits == 0b00) {
                bits -= 1;   // drop the async start bit added above
                return bits;
            }
            switch (registers[channel].r4_stop_bits) {
                case 0b01: bits += 1; break;      // 1 stop bit
                case 0b10: bits += 2; break;      // 1.5 stop bits (round up to 2)
                case 0b11: bits += 2; break;      // 2 stop bits
            }

            return bits;
        }

        // FORK: WR4 bits 5-4 = 10 and bits 3-2 = 00 is SDLC (Z8530 WR4).
        inline bool is_sdlc(scc_channel_t channel) const {
            return registers[channel].r4_sync_char_mode == 0b10 &&
                   registers[channel].r4_stop_bits == 0b00;
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
            event_timer->scheduleEvent(tx_due_14m[channel], tx_complete_callback, tx_timer_id[channel], this, "scc8530.tx_complete_callback", "src/devices/scc8530/Z85C30.hpp");
        }

        void schedule_rx_complete(scc_channel_t channel) {
            uint64_t ticks = 0;
            if (!event_timer || !clock) { rx_stalled[channel] = false; rx_complete(channel); return; }
            if (!char_ticks(channel, false, ticks)) { rx_stalled[channel] = true; return; }
            rx_stalled[channel] = false;
            rx_due_14m[channel] = clock->get_c14m() + ticks;
            event_timer->scheduleEvent(rx_due_14m[channel], rx_complete_callback, rx_timer_id[channel], this, "scc8530.rx_complete_callback", "src/devices/scc8530/Z85C30.hpp");
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
            event_timer->scheduleEvent(clock->get_c14m() + period, zc_callback, zc_timer_id[channel], this, "scc8530.zc_callback", "src/devices/scc8530/Z85C30.hpp");
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

        // Special receive condition (Z8530: overrun, CRC/framing, end of frame,
        // and parity when WR1 D2 makes it one). First-character mode (WR1 01)
        // still interrupts on a special condition after the first char; it is
        // re-armed for the *next* frame only by WR0 cmd 100 ($20), not by EOF.
        // EOF is visible when the last FCS byte is at the FIFO head.
        inline bool rx_special(scc_channel_t channel) {
            const auto &r = registers[channel];
            const bool eof = rx_fifo[channel].count > 0 && rx_fifo[channel].eof[0];
            return r.r1_rx_overrun_err || r.r1_crc_framing_err || eof
                || (r.r1_parity_err && r.r1_partity_spcond);
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
                    case 0b011: sdlc_tx[channel].clear(); break;                   // send abort (SDLC)
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
                    // FORK: WR0 bits 7-6 = 11 (ResetUnd $C0). XmitBegin/XmitEnd clear this
                    // before the last bytes; tx_complete sets it when the shifter underruns.
                    registers[channel].r0_tx_underrun_eom = 0;
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
            // FORK: WR3 D4 Enter Hunt Mode sets RR0 D4 Sync/Hunt (Z8530 WR3/RR0).
            // The bit stays set until a flag is found; writing D4=0 does not leave hunt.
            if (registers[channel].r3_hunt_mode) {
                registers[channel].r0_sync_hunt = 1;
            }
            push_line_params(channel);
            // FORK: do not inject at the RX-enable edge. That host-time patch
            // landed held ATPs during RecvInit (Path S). Solicited replies are
            // already on the due queue from tx_complete, at TX EOF + IFG.
            if (ltoudp_enabled()) {
                ltoudp_trace("wr3 data=$%02X rxen=%d due=%zu inbox=%zu",
                    (unsigned)data,
                    registers[channel].r3_rx_enable ? 1 : 0,
                    ltoudp_due.size(), ltoudp_inbox.size());
                ltoudp_note_atp1("wr3");
                if (ltoudp_after_data) {
                    ltoudp_peek_lap("wr3");
                    ltoudp_after_data = false;
                }
                ltoudp_collect(0);
                ltoudp_schedule_unsolicited();
                ltoudp_deliver_due();
            }
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
            if (ltoudp_enabled() && ltoudp_trace_on() && ltoudp_after_cts)
                ltoudp_note_rr0_tifgap();
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
            // FORK: RR1 D7 End of Frame is visible when the last FCS byte is the
            // next FIFO character (RecvBytes tests RR1 before reading sccDat).
            if (rx_fifo[channel].count > 0 && rx_fifo[channel].eof[0])
                registers[channel].r1_end_of_frame = 1;
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
            ltoudp_ifg_id = base_instance_id + 8;
            ltoudp_rx_byte_id[SCC_CHANNEL_A] = base_instance_id + 9;
            ltoudp_rx_byte_id[SCC_CHANNEL_B] = base_instance_id + 10;
            ltoudp_beacon_id = base_instance_id + 11;

            // clear the data structures on boot.
            memset(reg_select, 0, sizeof(reg_select));
            memset(registers, 0, sizeof(registers));
            registers[SCC_CHANNEL_A].r0_dcd = 1; registers[SCC_CHANNEL_A].r0_cts = 1;
            registers[SCC_CHANNEL_B].r0_dcd = 1; registers[SCC_CHANNEL_B].r0_cts = 1;
            reset();
            ltoudp_lockstep_bind();
        };

        ~Z85C30() { ltoudp_lockstep_unbind(); }

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
            rx_fifo[channel].eof[0] = rx_fifo[channel].eof[1] = rx_fifo[channel].eof[2] = 0;
            sdlc_tx[channel].clear();
            rx_pending[channel].clear();
            ltoudp_inbox.clear();
            ltoudp_due.clear();
            if (event_timer) event_timer->cancelEvents(ltoudp_ifg_id);
            ltoudp_ifg_scheduled = false;
            if (event_timer) event_timer->cancelEvents(ltoudp_beacon_id);
            ltoudp_beacon_scheduled = false;
            if (event_timer) event_timer->cancelEvents(ltoudp_rx_byte_id[channel]);
            ltoudp_rx_byte_scheduled[channel] = false;
            ltoudp_rx_due_14m[channel] = 0;
            ltoudp_rx_frame_n[channel] = 0;
            ltoudp_rx_frame_i[channel] = 0;
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
            rx_fifo[channel].eof[0] = rx_fifo[channel].eof[1] = rx_fifo[channel].eof[2] = 0;
            sdlc_tx[channel].clear();
            rx_pending[channel].clear();
            ltoudp_inbox.clear();
            ltoudp_due.clear();
            if (event_timer) event_timer->cancelEvents(ltoudp_ifg_id);
            ltoudp_ifg_scheduled = false;
            if (event_timer) event_timer->cancelEvents(ltoudp_beacon_id);
            ltoudp_beacon_scheduled = false;
            if (event_timer) event_timer->cancelEvents(ltoudp_rx_byte_id[channel]);
            ltoudp_rx_byte_scheduled[channel] = false;
            ltoudp_rx_due_14m[channel] = 0;
            ltoudp_rx_frame_n[channel] = 0;
            ltoudp_rx_frame_i[channel] = 0;
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
            // FORK: pull the socket without stalling; deliver only frames
            // whose guest-time IFG has already elapsed.
            if (ltoudp_enabled()) {
                ltoudp_collect(0);
                ltoudp_schedule_unsolicited();
                ltoudp_deliver_due();
            }
            if (ltoudp_enabled()) ltoudp_pace_rx();
            for (int channel = 0; channel < SCC_CHANNEL_COUNT; channel++) {
                rx_try_start((scc_channel_t)channel);
                if (ltoudp_enabled()) rx_poll_arm((scc_channel_t)channel);
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
                // FORK: an SDLC channel does not shift async host UART bytes into the
                // Rx FIFO. Those are not FM0/SDLC flags; delivering them would look like
                // a live cable. Layer 2 (LToUDP) will inject frames itself.
                if (msg.type == MESSAGE_DATA) {
                    if (is_sdlc(channel)) continue;
                    schedule_rx_char(channel, (uint8_t)msg.data);
                    return;
                }
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
                f.eof[f.count] = 0;
                f.data[f.count++] = data;
            } else {
                f.data[2] = data;
                f.eof[2] = 0;
                registers[channel].r1_rx_overrun_err = 1;
                if (SCDEBUG) printf("SCC: Ch %c RX overrun - FIFO full\n", ch_name(channel));
            }
            registers[channel].r0_rx_char_available = 1;
        }

        void rx_poll_arm(scc_channel_t channel) {
            if (!event_timer || !clock || rx_poll_scheduled[channel]) return;
            if (serial_devices[channel] == nullptr && !ltoudp_enabled()) return;
            rx_poll_scheduled[channel] = true;
            event_timer->scheduleEvent(clock->get_c14m() + RX_POLL_14M, rx_poll_callback, rx_poll_id[channel], this, "scc8530.rx_poll_callback", "src/devices/scc8530/Z85C30.hpp");
        }

        void rx_poll(scc_channel_t channel) {
            rx_poll_scheduled[channel] = false;
            if (ltoudp_enabled()) {
                ltoudp_collect(0);
                ltoudp_schedule_unsolicited();
                ltoudp_deliver_due();
            }
            if (ltoudp_enabled()) ltoudp_pace_rx();
            if (serial_devices[channel] != nullptr) rx_try_start(channel);
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
                f.eof[0] = f.eof[1];
                f.eof[1] = f.eof[2];
                f.eof[2] = 0;
                f.count--;
                if (ltoudp_enabled()) ltoudp_pace_rx();
                registers[channel].r0_rx_char_available = f.count > 0;
                if (f.count > 0 && f.eof[0]) registers[channel].r1_end_of_frame = 1;
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

        // ---- A2GSPU snapshot (M5): the chip -- registers, both channels' FIFOs and shift
        // registers, the interrupt block, the line inputs and their latches, and the time left
        // on a character in flight. The devices behind the ports are host connections and are
        // not saved; a restore tells the attached device the port's DTR, RTS and break.
        void A2GSPU_snapshot(FILE *f) {
            const uint64_t now = clock ? clock->get_c14m() : 0;
            for (int c = 0; c < SCC_CHANNEL_COUNT; c++) {
                const auto &r = registers[c];
                fwrite(r.w_regs, 1, 16, f);
                uint8_t rr[8] = { r.r_reg_0, r.r_reg_1, r.r_reg_3, r.r_reg_10, r.char_rx, r.char_tx,
                                  (uint8_t)r.tx_in_progress, (uint8_t)r.rx_in_progress };
                fwrite(rr, 1, 8, f);
                uint16_t sel = reg_select[c]; fwrite(&sel, 2, 1, f);
                fwrite(rx_fifo[c].data, 1, 3, f); fwrite(&rx_fifo[c].count, 1, 1, f);
                uint8_t fl[12] = { tx_shift[c], (uint8_t)tx_ip_latch[c], (uint8_t)ext_ip_latch[c], (uint8_t)rx_first_armed[c],
                                   (uint8_t)tx_stalled[c], (uint8_t)rx_stalled[c], (uint8_t)dcd_in[c], (uint8_t)cts_in[c],
                                   (uint8_t)break_in[c], (uint8_t)ext_latched[c], ext_latched_bits[c], ext_last_bits[c] };
                fwrite(fl, 1, 12, f);
                uint64_t left[2] = {
                    (r.tx_in_progress && !tx_stalled[c] && tx_due_14m[c] > now) ? tx_due_14m[c] - now : 0,
                    (r.rx_in_progress && !rx_stalled[c] && rx_due_14m[c] > now) ? rx_due_14m[c] - now : 0 };
                fwrite(left, 8, 2, f);
            }
            uint8_t ib[2] = { ip_bits, ius_bits }; fwrite(ib, 1, 2, f);
        }

        bool A2GSPU_restore(FILE *f) {
            struct saved_t { uint8_t w[16]; uint8_t rr[8]; uint16_t sel; uint8_t fifo[3]; uint8_t count; uint8_t fl[12]; uint64_t left[2]; };
            saved_t sv[SCC_CHANNEL_COUNT];
            for (int c = 0; c < SCC_CHANNEL_COUNT; c++) {
                saved_t &s = sv[c];
                if (fread(s.w, 1, 16, f) != 16 || fread(s.rr, 1, 8, f) != 8 || fread(&s.sel, 2, 1, f) != 1 ||
                    fread(s.fifo, 1, 3, f) != 3 || fread(&s.count, 1, 1, f) != 1 || fread(s.fl, 1, 12, f) != 12 ||
                    fread(s.left, 8, 2, f) != 2 || s.count > 3) return false;
            }
            uint8_t ib[2]; if (fread(ib, 1, 2, f) != 2) return false;
            const uint64_t now = clock ? clock->get_c14m() : 0;
            for (int c = 0; c < SCC_CHANNEL_COUNT; c++) {
                const scc_channel_t ch = (scc_channel_t)c;
                const saved_t &s = sv[c];
                if (event_timer) {
                    event_timer->cancelEvents(tx_timer_id[c]);
                    event_timer->cancelEvents(rx_timer_id[c]);
                    event_timer->cancelEvents(zc_timer_id[c]);
                }
                zc_scheduled[c] = false;
                auto &r = registers[c];
                memcpy(r.w_regs, s.w, 16);
                r.r_reg_0 = s.rr[0]; r.r_reg_1 = s.rr[1]; r.r_reg_3 = s.rr[2]; r.r_reg_10 = s.rr[3];
                r.char_rx = s.rr[4]; r.char_tx = s.rr[5]; r.tx_in_progress = s.rr[6] != 0; r.rx_in_progress = s.rr[7] != 0;
                reg_select[c] = s.sel;
                memcpy(rx_fifo[c].data, s.fifo, 3); rx_fifo[c].count = s.count;
                tx_shift[c] = s.fl[0]; tx_ip_latch[c] = s.fl[1] != 0; ext_ip_latch[c] = s.fl[2] != 0; rx_first_armed[c] = s.fl[3] != 0;
                tx_stalled[c] = s.fl[4] != 0; rx_stalled[c] = s.fl[5] != 0; dcd_in[c] = s.fl[6] != 0; cts_in[c] = s.fl[7] != 0;
                break_in[c] = s.fl[8] != 0; ext_latched[c] = s.fl[9] != 0; ext_latched_bits[c] = s.fl[10]; ext_last_bits[c] = s.fl[11];
                clock_mode[c] = clock_mode_table[r.r4_clock_mode];
                uint64_t hz = 0, div = 1;
                baud_rate[c] = (clock_source(ch, true, hz, div) && hz) ? (float)((double)hz / ((double)clock_mode[c] * (double)div)) : 0.0f;
                if (r.tx_in_progress && !tx_stalled[c] && event_timer && clock) {
                    tx_due_14m[c] = now + (s.left[0] ? s.left[0] : 1);
                    event_timer->scheduleEvent(tx_due_14m[c], tx_complete_callback, tx_timer_id[c], this, "scc8530.tx_complete_callback", "src/devices/scc8530/Z85C30.hpp");
                }
                if (r.rx_in_progress && !rx_stalled[c] && event_timer && clock) {
                    rx_due_14m[c] = now + (s.left[1] ? s.left[1] : 1);
                    event_timer->scheduleEvent(rx_due_14m[c], rx_complete_callback, rx_timer_id[c], this, "scc8530.rx_complete_callback", "src/devices/scc8530/Z85C30.hpp");
                }
                zc_arm(ch);
                rx_poll_arm(ch);
                line_params_last[c] = ~0ull;
                push_line_params(ch);
                lines_out_update(ch, true);
            }
            ip_bits = ib[0]; ius_bits = ib[1];
            update_interrupts();
            return true;
        }

        void debug_output(DebugFormatter *df,bool native_only=false) {
            if(native_only) {
            for(int c=0;c<SCC_CHANNEL_COUNT;c++) {
                const std::string prefix=c==SCC_CHANNEL_A?"a.":"b.";
                df->unsignedField(prefix+"register_select",reg_select[c],8);
                df->bytesField(prefix+"write_registers",registers[c].w_regs,16);
                df->unsignedField(prefix+"rr0",registers[c].r_reg_0,8);
                df->unsignedField(prefix+"rr1",registers[c].r_reg_1,8);
                df->unsignedField(prefix+"rx_count",rx_fifo[c].count,8,"bytes");
                df->bytesField(prefix+"rx_fifo_storage",rx_fifo[c].data,3);
                df->boolField(prefix+"rx_busy",registers[c].rx_in_progress);
                df->boolField(prefix+"tx_busy",registers[c].tx_in_progress);
                df->boolField(prefix+"tx_buffer_empty",registers[c].r0_tx_buffer_empty);
                df->boolField(prefix+"dcd",dcd_in[c]);
                df->boolField(prefix+"cts",cts_in[c]);
                df->boolField(prefix+"break",break_in[c]);
                df->boolField(prefix+"tx_stalled",tx_stalled[c]);
                df->boolField(prefix+"rx_stalled",rx_stalled[c]);
            }
            return;
            }
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


        // Pending LToUDP bytes enter the 3-byte FIFO only via
        // ltoudp_pace_rx, one character every LTOUDP_BYTE_14M (230.4 kbps FM0).
        // Instantly topping up the FIFO made a 39-byte frame available at CPU
        // speed and let PacketIRQ/RecvInit see the header during RecvInit.
        void rx_feed_pending(scc_channel_t channel) {
            (void)channel;
        }


        // FORK: A2GSPU_LTOUDP_TRACE=<path> logs LToUDP drain/inject (channel, WR6
        // address, FIFO/pending, first-char armed, RR3 IP/IUS, MIE). Empty drains
        // are silent. A pure diagnostic; no guest-visible change when unset.
        bool ltoudp_trace_on() const {
            static int st = -1;
            if (st < 0) {
                const char *e = std::getenv("A2GSPU_LTOUDP_TRACE");
                st = (e && e[0]) ? 1 : 0;
            }
            return st != 0;
        }
        uint32_t ltoudp_guest_pc() const {
            return z85c30_query_guest_pc_safe();
        }
        uint64_t ltoudp_guest_cycles() const {
            return clock ? clock->get_cycles() : 0;
        }
        void ltoudp_trace(const char *fmt, ...) {
            static FILE *fp = (FILE *)-1;
            if (fp == (FILE *)-1) {
                const char *e = std::getenv("A2GSPU_LTOUDP_TRACE");
                fp = (e && e[0]) ? std::fopen(e, "w") : nullptr;
            }
            if (!fp) return;
            std::fprintf(fp, "c14m=%llu cyc=%llu pc=%06X ",
                (unsigned long long)ltoudp_now_14m(),
                (unsigned long long)ltoudp_guest_cycles(),
                (unsigned)ltoudp_guest_pc());
            std::va_list ap;
            va_start(ap, fmt);
            std::vfprintf(fp, fmt, ap);
            va_end(ap);
            std::fputc('\n', fp);
            std::fflush(fp);
        }
        void ltoudp_trace_ch(const char *tag, scc_channel_t channel,
                             uint8_t dest, size_t n, const char *why) {
            static FILE *probe = (FILE *)-1;
            if (probe == (FILE *)-1) {
                const char *e = std::getenv("A2GSPU_LTOUDP_TRACE");
                probe = (e && e[0]) ? (FILE *)1 : nullptr;
            }
            if (!probe) return;
            const auto &r = registers[channel];
            const bool mie = registers[SCC_CHANNEL_A].r9_mie != 0;
            if (why)
                ltoudp_trace("%s ch=%c dest=$%02X n=%zu why=%s wr6=$%02X wr3=$%02X wr1=$%02X rxen=%d sdlc=%d hunt=%d avail=%d fifo=%u pend=%zu armed=%d ip=$%02X ius=$%02X mie=%d i=%d",
                    tag, ch_name(channel), dest, n, why,
                    (unsigned)r.r6, (unsigned)r.w_regs[WR3], (unsigned)r.w_regs[WR1],
                    r.r3_rx_enable ? 1 : 0, is_sdlc(channel) ? 1 : 0,
                    r.r0_sync_hunt ? 1 : 0, r.r0_rx_char_available ? 1 : 0,
                    (unsigned)rx_fifo[channel].count, rx_pending[channel].size(),
                    rx_first_armed[channel] ? 1 : 0,
                    (unsigned)ip_bits, (unsigned)ius_bits, mie ? 1 : 0,
                    (z85c30_query_guest_p_safe() & 0x04) ? 1 : 0);
            else
                ltoudp_trace("%s ch=%c dest=$%02X n=%zu wr6=$%02X wr3=$%02X wr1=$%02X rxen=%d sdlc=%d hunt=%d avail=%d fifo=%u pend=%zu armed=%d ip=$%02X ius=$%02X mie=%d i=%d",
                    tag, ch_name(channel), dest, n,
                    (unsigned)r.r6, (unsigned)r.w_regs[WR3], (unsigned)r.w_regs[WR1],
                    r.r3_rx_enable ? 1 : 0, is_sdlc(channel) ? 1 : 0,
                    r.r0_sync_hunt ? 1 : 0, r.r0_rx_char_available ? 1 : 0,
                    (unsigned)rx_fifo[channel].count, rx_pending[channel].size(),
                    rx_first_armed[channel] ? 1 : 0,
                    (unsigned)ip_bits, (unsigned)ius_bits, mie ? 1 : 0,
                    (z85c30_query_guest_p_safe() & 0x04) ? 1 : 0);
        }

        // FORK: accept a raw LLAP frame if this SDLC channel is addressed, append
        // CRC-16/X.25, leave hunt, and pulse $C044/$C045. RR1 EOF is set on the
        // last CRC byte (visible when that byte is next in the FIFO).
        bool ltoudp_try_inject(scc_channel_t channel, const uint8_t *llap, size_t n) {
            const uint8_t dest = (n >= 1) ? llap[0] : 0;
            if (!is_sdlc(channel) || !registers[channel].r3_rx_enable) {
                return false;
            }
            if (n < 3 || n > 603) {
                return false;
            }
            // LTG1 time controls are not LLAP; never inject dest=0 type=$C0.
            if (dest == 0 && llap[1] == 0 && llap[2] == LTOUDP_LAP_TIME) {
                return false;
            }
            if (registers[channel].r3_address_search_mode) {
                if (dest != 0xFF && dest != registers[channel].r6) {
                    return false;
                }
            }
            // RecvBytes LastByte does not consume the last FCS byte. A leftover
            // EOF byte at the FIFO head would otherwise prefix the next frame.
            auto &f = rx_fifo[channel];
            if (f.count == 1 && f.eof[0]) {
                f.count = 0;
                f.eof[0] = 0;
                if (rx_pending[channel].empty())
                    registers[channel].r0_rx_char_available = 0;
            }
            uint16_t crc = ltoudp_crc16_x25(llap, n);
            for (size_t i = 0; i < n; i++)
                rx_pending[channel].push_back(rx_pend_t{ llap[i], 0 });
            rx_pending[channel].push_back(rx_pend_t{ (uint8_t)(crc & 0xFF), 0 });
            rx_pending[channel].push_back(rx_pend_t{ (uint8_t)(crc >> 8), 1 });
            registers[channel].r0_sync_hunt = 0;
            iigs_line_activity_pulse(channel == SCC_CHANNEL_A);
            // First data bit-cell starts now; the first FIFO byte is due
            // one LocalTalk character later (~34.7 us).
            const size_t wire_n = rx_pending[channel].size();
            ltoudp_rx_frame_n[channel] = wire_n;
            ltoudp_rx_frame_i[channel] = 0;
            ltoudp_rx_due_14m[channel] = ltoudp_now_14m() + LTOUDP_BYTE_14M;
            ltoudp_arm_rx_byte_timer(channel);
            ltoudp_pace_rx();
            update_interrupts(channel);
            ltoudp_trace_ch("inject-ok", channel, dest, n, nullptr);
            return true;
        }

        // FORK: recv LToUDP frames and inject into every addressed SDLC channel.
        // timeout_ms > 0 stalls the host (guest clock frozen) to catch a peer reply.
        // ltoudp_rx_ready is the half-duplex gate: the next datagram is injected
        // only after the previous frame has left rx_pending / the FIFO.
        bool ltoudp_rx_ready(scc_channel_t channel) const {
            if (!is_sdlc(channel) || !registers[channel].r3_rx_enable) return false;
            if (!rx_pending[channel].empty()) return false;
            const auto &f = rx_fifo[channel];
            return f.count == 0 || (f.count == 1 && f.eof[0]);
        }


        // Learn ATP1's loaded bank from the first post-CTS WR3 whose
        // 16-bit offset sits in RecvInit ($1A5B-$1A6C in atrom.map).
        const char *ltoudp_atp1_name(uint32_t pc) const {
            if (!ltoudp_atp1_bank_known) return nullptr;
            if ((pc & 0xFF0000) != ltoudp_atp1_bank) return nullptr;
            const uint32_t off = pc & 0xFFFF;
            if (off >= 0x16B0 && off <= 0x1755) return "RecvPacket/TIFGap";
            if (off >= 0x1A5B && off <= 0x1A6C) return "RecvInit";
            if (off >= 0x18F5 && off <= 0x192B) return "fXmitLAP";
            if (off >= 0x1442 && off <= 0x1580) return "PacketIRQ";
            return nullptr;
        }
        void ltoudp_note_atp1(const char *site) {
            if (!ltoudp_trace_on()) return;
            const uint32_t pc = ltoudp_guest_pc();
            const uint32_t off = pc & 0xFFFF;
            if (ltoudp_after_cts && !ltoudp_atp1_bank_known &&
                off >= 0x1A5B && off <= 0x1A6C) {
                ltoudp_atp1_bank = pc & 0xFF0000;
                ltoudp_atp1_bank_known = true;
                ltoudp_trace("atp1-base bank=$%02X0000 site=%s",
                    (unsigned)(ltoudp_atp1_bank >> 16), site);
            }
            const char *reg = ltoudp_atp1_name(pc);
            if (reg != ltoudp_atp1_reg) {
                if (ltoudp_atp1_reg)
                    ltoudp_trace("atp1-end %s start_c14m=%llu start_pc=%06X site=%s",
                        ltoudp_atp1_reg,
                        (unsigned long long)ltoudp_atp1_reg_14m,
                        (unsigned)ltoudp_atp1_reg_pc, site);
                ltoudp_atp1_reg = reg;
                ltoudp_atp1_reg_14m = ltoudp_now_14m();
                ltoudp_atp1_reg_pc = pc;
                if (reg)
                    ltoudp_trace("atp1-start %s site=%s", reg, site);
            }
        }
        // One RR0 sample at TIFGap start and one at end. Not per-poll.
        void ltoudp_note_rr0_tifgap() {
            if (!ltoudp_trace_on() || !ltoudp_after_cts) return;
            const uint32_t pc = ltoudp_guest_pc();
            const char *reg = ltoudp_atp1_name(pc);
            if (!ltoudp_tifgap_open && reg && std::strcmp(reg, "RecvPacket/TIFGap") == 0) {
                ltoudp_tifgap_open = true;
                ltoudp_trace("tifgap-start");
                ltoudp_note_atp1("rr0");
            } else if (ltoudp_tifgap_open && (!reg || std::strcmp(reg, "RecvPacket/TIFGap") != 0)) {
                ltoudp_trace("tifgap-end site=rr0");
                ltoudp_tifgap_open = false;
                ltoudp_after_cts = false;
                ltoudp_note_atp1("rr0-end");
            }
        }

        uint64_t ltoudp_now_14m() const {
            return clock ? clock->get_c14m() : ltoudp_test_now;
        }

        void ltoudp_push_rx_byte(scc_channel_t channel) {
            auto &p = rx_pending[channel];
            if (p.empty()) return;
            rx_pend_t b = p.front();
            p.erase(p.begin());
            auto &f = rx_fifo[channel];
            const size_t i = ltoudp_rx_frame_i[channel]++;
            const size_t n = ltoudp_rx_frame_n[channel];
            int overrun = 0;
            if (f.count < 3) {
                f.data[f.count] = b.data;
                f.eof[f.count] = b.eof;
                f.count++;
            } else {
                // Z8530: a fourth character with the FIFO full overwrites
                // the newest entry and sets Rx Overrun (latched until Error Reset).
                f.data[2] = b.data;
                f.eof[2] = b.eof;
                registers[channel].r1_rx_overrun_err = 1;
                overrun = 1;
            }
            registers[channel].r0_rx_char_available = 1;
            if (f.count > 0 && f.eof[0]) registers[channel].r1_end_of_frame = 1;
            if (b.eof) {
                ltoudp_rx_eof_14m = ltoudp_now_14m();
                // Last-byte RX tick of a peer frame: tell the LTG1 peer so
                // its CTS window is rts_delivered_end, not the beacon clock.
                ltoudp_send_delivered(ltoudp_rx_eof_14m);
            }
            if (ltoudp_trace_on() && (i == 0 || b.eof || overrun))
                ltoudp_trace("rx-byte i=%zu/%zu data=$%02X fifo=%u pend=%zu eof=%d overrun=%d rr1_eof=%d crc=%d avail=%d fc_arm=%d i=%d pc=%06X",
                    i, n, (unsigned)b.data, (unsigned)f.count, p.size(),
                    b.eof ? 1 : 0, overrun,
                    (f.count > 0 && f.eof[0]) ? 1 : 0,
                    registers[channel].r1_crc_framing_err ? 1 : 0,
                    1,
                    rx_first_armed[channel] ? 1 : 0,
                    (z85c30_query_guest_p_safe() & 0x04) ? 1 : 0,
                    (unsigned)ltoudp_guest_pc());
            update_interrupts(channel);
        }

        static void ltoudp_rx_byte_callback(uint64_t instanceID, void* userData) {
            Z85C30* scc = static_cast<Z85C30*>(userData);
            if (instanceID == scc->ltoudp_rx_byte_id[SCC_CHANNEL_A])
                scc->ltoudp_rx_byte_scheduled[SCC_CHANNEL_A] = false;
            else if (instanceID == scc->ltoudp_rx_byte_id[SCC_CHANNEL_B])
                scc->ltoudp_rx_byte_scheduled[SCC_CHANNEL_B] = false;
            scc->ltoudp_pace_rx();
        }

        void ltoudp_arm_rx_byte_timer(scc_channel_t channel) {
            if (!event_timer || !clock) return;
            if (rx_pending[channel].empty()) {
                if (ltoudp_rx_byte_scheduled[channel]) {
                    event_timer->cancelEvents(ltoudp_rx_byte_id[channel]);
                    ltoudp_rx_byte_scheduled[channel] = false;
                }
                return;
            }
            if (ltoudp_rx_byte_scheduled[channel])
                event_timer->cancelEvents(ltoudp_rx_byte_id[channel]);
            event_timer->scheduleEvent(ltoudp_rx_due_14m[channel],
                ltoudp_rx_byte_callback, ltoudp_rx_byte_id[channel], this,
                "scc8530.ltoudp_rx_byte_callback", "src/devices/scc8530/Z85C30.hpp");
            ltoudp_rx_byte_scheduled[channel] = true;
        }

        void ltoudp_pace_rx() {
            const uint64_t now = ltoudp_now_14m();
            for (int c = 0; c < SCC_CHANNEL_COUNT; c++) {
                auto ch = (scc_channel_t)c;
                while (!rx_pending[ch].empty() && now >= ltoudp_rx_due_14m[ch]) {
                    ltoudp_push_rx_byte(ch);
                    if (!rx_pending[ch].empty())
                        ltoudp_rx_due_14m[ch] += LTOUDP_BYTE_14M;
                }
                ltoudp_arm_rx_byte_timer(ch);
            }
        }


        uint64_t ltoudp_horizon_limit() const {
            if (!ltoudp_lockstep || !ltoudp_peer_ltg1) return ~uint64_t(0);
            const uint64_t now = ltoudp_now_14m();
            // A grant already behind the guest is stale (a tight due of a
            // frame that has been sent, or a late beacon). Do not freeze
            // C14M on a past tick. A grant equal to now is a live horizon.
            if (ltoudp_grant_14m < now) return ~uint64_t(0);
            return ltoudp_grant_14m;
        }

        void ltoudp_apply_grant(const ltoudp_pkt &pkt) {
            if (!pkt.has_guest) return;
            uint64_t g = pkt.grant_14m;
            // An in-hand LLAP has already left the peer. The next earliest
            // new origin is after this frame leaves the wire plus one IDG.
            // If the frame expects a guest reply, the horizon also covers
            // the reply deadline so the guest can ACK/CTS/TResp (spec:
            // Docs/LToUDP.md grant semantics).
            if (pkt.kind == LTOUDP_RECV_LLAP && pkt.due_14m && pkt.n >= 3) {
                const uint64_t eof = pkt.due_14m
                    + (pkt.n + 2) * LTOUDP_BYTE_14M;
                uint64_t implicit = eof + LTOUDP_IDG_14M;
                if (ltoudp_expects_reply(pkt.llap, pkt.n)) {
                    const uint64_t deadline = (pkt.llap[2] == 0x85)
                        ? eof + LTOUDP_IFG_14M
                        : eof + LTOUDP_TIFGAP1_14M + 5ull * LTOUDP_BYTE_14M;
                    if (deadline > implicit) implicit = deadline;
                }
                if (g < implicit) g = implicit;
            }
            if (g == 0) return;
            const uint64_t now = ltoudp_now_14m();
            // A stale grant (behind the guest) must be ignored, not
            // installed as 'now'. Capping at now freezes the guest to
            // one cycle per heartbeat and makes rail 'run N' miss its
            // host deadline (seen in a harness run: run 2100 never acked,
            // zero horizon-peer-faults, ENQs every 2 s of host time).
            if (g < now) return;
            // Null-message rule: a grant that does not account for the
            // latest guest TX is not trusted past the post-TX clamp.
            // Older 4-word controls have accounted=0 and cannot lift it.
            // An in-hand peer LLAP is a frame (the obligation is met)
            // and may extend past the clamp.
            if (ltoudp_clamp_14m && ltoudp_tx_eof_14m
                && pkt.kind != LTOUDP_RECV_LLAP
                && pkt.accounted_14m < ltoudp_tx_eof_14m
                && g > ltoudp_clamp_14m) {
                g = ltoudp_clamp_14m;
            } else if (ltoudp_clamp_14m && ltoudp_tx_eof_14m
                       && (pkt.kind == LTOUDP_RECV_LLAP
                           || pkt.accounted_14m >= ltoudp_tx_eof_14m)) {
                ltoudp_clamp_14m = 0;
            }
            ltoudp_peer_ltg1 = true;
            ltoudp_lockstep = true;
            ltoudp_grant_14m = g;
        }

        void ltoudp_horizon_wait(uint64_t want) {
            if (ltoudp_in_horizon_wait) return;
            if (!ltoudp_lockstep || !ltoudp_peer_ltg1) return;
            ltoudp_in_horizon_wait = true;
            const auto wall0 = std::chrono::steady_clock::now();
            while (ltoudp_lockstep && ltoudp_grant_14m < want) {
                const auto used = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - wall0).count();
                if (used >= LTOUDP_LTG1_SAFETY_MS) {
                    ltoudp_horizon_faults++;
                    ltoudp_trace("horizon-peer-fault want=%llu grant=%llu wall_ms=%lld faults=%llu (safety bound, lift cap)",
                        (unsigned long long)want,
                        (unsigned long long)ltoudp_grant_14m,
                        (long long)used,
                        (unsigned long long)ltoudp_horizon_faults);
                    ltoudp_grant_14m = ~uint64_t(0);
                    break;
                }
                int wait = 5;
                const auto rem = (long long)LTOUDP_LTG1_SAFETY_MS - used;
                if (rem < wait) wait = rem > 1 ? (int)rem : 1;
                ltoudp_collect(wait);
            }
            ltoudp_schedule_unsolicited();
            ltoudp_deliver_due();
            ltoudp_in_horizon_wait = false;
        }

        void ltoudp_lockstep_bind() {
            ltoudp_hook_scc = this;
            nclock_hooks::c14m_limit = []() -> uint64_t {
                return ltoudp_hook_scc ? ltoudp_hook_scc->ltoudp_horizon_limit() : ~uint64_t(0);
            };
            nclock_hooks::c14m_wait = [](uint64_t want) {
                if (ltoudp_hook_scc) ltoudp_hook_scc->ltoudp_horizon_wait(want);
            };
        }

        void ltoudp_lockstep_unbind() {
            if (ltoudp_hook_scc == this) {
                nclock_hooks::c14m_limit = nullptr;
                nclock_hooks::c14m_wait = nullptr;
                ltoudp_hook_scc = nullptr;
            }
        }

        void ltoudp_advance_14m(uint64_t ticks) {
            if (ltoudp_lockstep && ltoudp_peer_ltg1) {
                const uint64_t now0 = ltoudp_now_14m();
                uint64_t target = now0 + ticks;
                if (target > ltoudp_grant_14m) {
                    ltoudp_horizon_wait(target);
                    const uint64_t now1 = ltoudp_now_14m();
                    uint64_t allow = ltoudp_grant_14m;
                    if (target > allow) target = allow;
                    ticks = (target > now1) ? (target - now1) : 0;
                }
            }
            if (!clock) ltoudp_test_now += ticks;
            ltoudp_deliver_due();
            ltoudp_pace_rx();
        }

        static uint8_t ltoudp_llap_type(const uint8_t *llap, size_t n) {
            return (n >= 3) ? llap[2] : 0;
        }

        static bool ltoudp_is_data_llap(const uint8_t *llap, size_t n) {
            if (n < 3) return false;
            const uint8_t t = llap[2];
            return t == 0x01 || t == 0x02;
        }

        // ATP function in bits 7-6 of the control byte (1=TReq, 2=TResp, 3=TRel).
        // Short DDP (LLAP $01): sockets + DDP type at offset 5, ATP at 6.
        // Extended DDP (LLAP $02): DDP type at offset 13, ATP at 14.
        static int ltoudp_atp_fn(const uint8_t *llap, size_t n) {
            size_t off;
            if (n >= 7 && llap[2] == 0x01) {
                if (llap[5] != 3) return -1;
                off = 6;
            } else if (n >= 15 && llap[2] == 0x02) {
                if (llap[13] != 3) return -1;
                off = 14;
            } else {
                return -1;
            }
            if (n <= off) return -1;
            return (int)((llap[off] >> 6) & 0x3);
        }

        static bool ltoudp_expects_reply(const uint8_t *llap, size_t n) {
            if (n < 3) return false;
            const uint8_t t = llap[2];
            if (t == 0x81 || t == 0x84 || t == 0x85) return true; // ENQ, RTS, CTS
            return ltoudp_atp_fn(llap, n) == 1;                 // ATP TReq
        }

        uint64_t ltoudp_line_idle_14m(uint64_t gap = LTOUDP_IFG_14M) const {
            uint64_t idle = ltoudp_now_14m();
            if (ltoudp_tx_eof_14m && ltoudp_tx_eof_14m + gap > idle)
                idle = ltoudp_tx_eof_14m + gap;
            if (ltoudp_rx_eof_14m && ltoudp_rx_eof_14m + gap > idle)
                idle = ltoudp_rx_eof_14m + gap;
            for (const auto &d : ltoudp_due) {
                const uint64_t nch = d.llap.size() + 2; // data + FCS
                const uint64_t end = d.due_14m + nch * LTOUDP_BYTE_14M + gap;
                if (end > idle) idle = end;
            }
            for (int c = 0; c < SCC_CHANNEL_COUNT; c++) {
                auto ch = (scc_channel_t)c;
                if (rx_pending[ch].empty()) continue;
                const size_t left = rx_pending[ch].size();
                uint64_t end = ltoudp_rx_due_14m[ch] + left * LTOUDP_BYTE_14M + gap;
                if (end > idle) idle = end;
            }
            return idle;
        }

        bool ltoudp_on_wire() const {
            if (!ltoudp_due.empty() || !ltoudp_inbox.empty()) return true;
            for (int c = 0; c < SCC_CHANNEL_COUNT; c++) {
                if (!rx_pending[c].empty()) return true;
                if (ltoudp_rx_frame_n[c] && ltoudp_rx_frame_i[c] < ltoudp_rx_frame_n[c])
                    return true;
            }
            return false;
        }

        void ltoudp_send_beacon() {
            if (!ltoudp_enabled()) return;
            const uint64_t now = ltoudp_now_14m();
            ltoudp_send_time(LTOUDP_TIME_BEACON, now, ltoudp_line_idle_14m(),
                             ltoudp_rx_eof_14m);
            ltoudp_last_beacon_14m = now;
        }

        void ltoudp_send_delivered(uint64_t eof_14m) {
            if (!ltoudp_enabled() || !ltoudp_peer_ltg1) return;
            ltoudp_send_time(LTOUDP_TIME_DELIVERED, eof_14m,
                             ltoudp_line_idle_14m(), eof_14m);
            ltoudp_trace("delivered-at eof=%llu idle=%llu type_last=rx",
                (unsigned long long)eof_14m,
                (unsigned long long)ltoudp_line_idle_14m());
        }

        void ltoudp_arm_beacon_timer() {
            if (!event_timer || !clock || !ltoudp_enabled()) return;
            const uint64_t due = ltoudp_now_14m() + LTOUDP_BEACON_14M;
            if (ltoudp_beacon_scheduled) event_timer->cancelEvents(ltoudp_beacon_id);
            event_timer->scheduleEvent(due, ltoudp_beacon_callback, ltoudp_beacon_id, this,
                "scc8530.ltoudp_beacon_callback", "src/devices/scc8530/Z85C30.hpp");
            ltoudp_beacon_scheduled = true;
        }

        static void ltoudp_beacon_callback(uint64_t instanceID, void* userData) {
            (void)instanceID;
            Z85C30* scc = static_cast<Z85C30*>(userData);
            scc->ltoudp_beacon_scheduled = false;
            scc->ltoudp_send_beacon();
            scc->ltoudp_arm_beacon_timer();
            scc->ltoudp_drain(0);
        }


        // Pull socket datagrams into the inbox. timeout_ms > 0 freezes the
        // guest until one datagram arrives or the host timeout expires.
        uint64_t ltoudp_reply_deadline_14m(const uint8_t *tx, size_t n) const {
            const uint64_t eof = ltoudp_tx_eof_14m ? ltoudp_tx_eof_14m : ltoudp_now_14m();
            if (tx && n >= 3 && tx[2] == 0x85)
                return eof + LTOUDP_IFG_14M;
            return eof + LTOUDP_TIFGAP1_14M + 5ull * LTOUDP_BYTE_14M;
        }

        // Bank $E1 AppleTalk LAP vars (lap_includes.aii E1Start=$1000).
        void ltoudp_peek_lap(const char *site) {
            if (!ltoudp_trace_on()) return;
            const char *off = std::getenv("A2GSPU_LTOUDP_PEEK");
            if (off && (off[0] == '0' || off[0] == 'n' || off[0] == 'N' || off[0] == 'f' || off[0] == 'F'))
                return;
            uint8_t flags[4] = {0, 0, 0, 0};
            uint8_t got[2] = {0, 0};
            uint8_t grq[4] = {0, 0, 0, 0};
            const int a = z85c30_query_guest_peek_safe(0xE11051, flags, 4);
            const int b = z85c30_query_guest_peek_safe(0xE11092, got, 2);
            const int c = z85c30_query_guest_peek_safe(0xE1DE56, grq, 4);
            if (a != 0 && b != 0 && c != 0) {
                ltoudp_trace("peek-lap site=%s pc=%06X (guest peek unavailable)",
                    site, (unsigned)ltoudp_guest_pc());
                return;
            }
            const uint32_t grqhead = (uint32_t)grq[0] | ((uint32_t)grq[1] << 8)
                | ((uint32_t)grq[2] << 16) | ((uint32_t)grq[3] << 24);
            int have81 = 0, ngrq = 0;
            uint32_t node = grqhead;
            for (int i = 0; i < 8 && node; i++) {
                uint8_t skt = 0, link[4] = {0, 0, 0, 0};
                if (z85c30_query_guest_peek_safe(node + 8, &skt, 1) != 0) break;
                ngrq++;
                if (skt == 0x81) have81 = 1;
                if (z85c30_query_guest_peek_safe(node + 29, link, 4) != 0) break;
                uint32_t next = (uint32_t)link[0] | ((uint32_t)link[1] << 8)
                    | ((uint32_t)link[2] << 16) | ((uint32_t)link[3] << 24);
                if (next == node) break;
                node = next;
            }
            ltoudp_trace("peek-lap site=%s pc=%06X needData=$%02X addrValid=$%02X busyFlag=$%02X pktError=$%02X gotCTS=$%02X gotData=$%02X grqhead=$%08X ngrq=%d skt81=%d",
                site, (unsigned)ltoudp_guest_pc(),
                (unsigned)flags[0], (unsigned)flags[1], (unsigned)flags[2], (unsigned)flags[3],
                (unsigned)got[0], (unsigned)got[1],
                (unsigned)grqhead, ngrq, have81);
        }

        // Pull socket datagrams into the inbox. timeout_ms > 0 freezes the
        // guest until one datagram arrives or the host timeout expires.
        // LTG1 solicited collects (ltg1_reply) end on guest-time evidence:
        // a reply stamped for this TX, or a peer beacon/no-reply whose
        // now is past the CTS/IFG window. The host bound is a safety
        // valve logged as a peer fault, never a silent collision.
        // Plain peers keep the documented 5 ms host-time collect.
        void ltoudp_collect(int timeout_ms, const uint8_t *expect_tx = nullptr,
                            size_t expect_n = 0, bool ltg1_reply = false) {
            if (!ltoudp_enabled()) return;
            const uint64_t c0 = ltoudp_now_14m();
            const uint64_t y0 = ltoudp_guest_cycles();
            const auto wall0 = std::chrono::steady_clock::now();
            ltoudp_last_collect_reason = (timeout_ms > 0) ? "timeout" : "none";
            ltoudp_last_reply_host_us = -1;
            const uint64_t deadline = (expect_tx && expect_n >= 3)
                ? ltoudp_reply_deadline_14m(expect_tx, expect_n) : 0;
            if (timeout_ms > 0)
                ltoudp_trace("collect-start timeout=%d ltg1=%d c14m=%llu cyc=%llu deadline=%llu",
                    timeout_ms, ltg1_reply ? 1 : 0,
                    (unsigned long long)c0, (unsigned long long)y0,
                    (unsigned long long)deadline);
            size_t recvd = 0;
            // Sequenced keepalives while C14M is frozen so a lost last frame
            // is visible as a gap (Docs/LToUDP.md Reliability). grant=0 so
            // apply_grant will not move the horizon.
            const bool start_ka = timeout_ms > 0 && (ltg1_reply || ltoudp_peer_ltg1);
            if (start_ka) ltoudp_send_beacon();
            for (;;) {
                int wait = 0;
                if (timeout_ms > 0) {
                    const auto used = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - wall0).count();
                    if (used >= timeout_ms) break;
                    wait = (int)(timeout_ms - used);
                    if ((ltg1_reply || ltoudp_peer_ltg1) && wait > 50)
                        wait = 50;
                }
                ltoudp_pkt pkt {};
                int kind = ltoudp_recv_pkt(&pkt, wait);
                if (kind == LTOUDP_RECV_NONE) {
                    if (timeout_ms > 0 && (ltg1_reply || ltoudp_peer_ltg1)) {
                        const auto used = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - wall0).count();
                        if (used < timeout_ms) {
                            ltoudp_send_beacon();
                            continue;
                        }
                    }
                    break;
                }
                if (kind == LTOUDP_RECV_TIME) {
                    if (pkt.has_guest) { ltoudp_peer_ltg1 = true; ltoudp_apply_grant(pkt); }
                    if (pkt.time_cmd == 0) ltoudp_send_beacon();
                    if (ltg1_reply && pkt.has_guest) {
                        // The guest is frozen, so clock.now cannot walk
                        // past the window. An idle-lookahead grant (500 ms)
                        // is always past the ~622 us deadline and must not
                        // end collect by itself (it would drop a CTS/ACK
                        // already on the wire). Only TIME_NOREPLY means
                        // the peer will not reply.
                        if (pkt.time_cmd == LTOUDP_TIME_NOREPLY) {
                            ltoudp_last_collect_reason = "noreply";
                            ltoudp_trace("collect-noreply cmd=%u now=%llu grant=%llu deadline=%llu",
                                (unsigned)pkt.time_cmd,
                                (unsigned long long)pkt.now_14m,
                                (unsigned long long)pkt.grant_14m,
                                (unsigned long long)deadline);
                            break;
                        }
                    }
                    if (timeout_ms > 0) {
                        const auto used = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - wall0).count();
                        wait = (used < timeout_ms) ? (int)(timeout_ms - used) : 0;
                        if (wait > 0) continue;
                    }
                    continue;
                }
                if (kind != LTOUDP_RECV_LLAP || pkt.n < 3) {
                    if (timeout_ms > 0) {
                        const auto used = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - wall0).count();
                        wait = (used < timeout_ms) ? (int)(timeout_ms - used) : 0;
                        if (wait > 0) continue;
                    }
                    break;
                }
                if (pkt.has_guest) { ltoudp_peer_ltg1 = true; ltoudp_apply_grant(pkt); }
                ltoudp_in_t evicted;
                bool park = false;
                if (!ltoudp_inbox.empty()) {
                    if (timeout_ms > 0) {
                        ltoudp_trace("collect-evict n=%zu type=$%02X inbox=%zu due=%zu (solicited owns the line)",
                            pkt.n, (unsigned)pkt.llap[2],
                            ltoudp_inbox.size(), ltoudp_due.size());
                        evicted = std::move(ltoudp_inbox.front());
                        ltoudp_inbox.erase(ltoudp_inbox.begin());
                        park = true;
                    } else {
                        ltoudp_trace("drop-pile n=%zu type=$%02X inbox=%zu due=%zu",
                            pkt.n, (unsigned)pkt.llap[2],
                            ltoudp_inbox.size(), ltoudp_due.size());
                        break;
                    }
                }
                ltoudp_in_t fr(pkt.llap, pkt.llap + pkt.n);
                if (pkt.has_guest && pkt.due_14m) {
                    fr.due_14m = pkt.due_14m;
                    fr.has_due = true;
                }
                ltoudp_inbox.push_back(std::move(fr));
                if (park) ltoudp_inbox.push_back(std::move(evicted));
                recvd++;
                if (timeout_ms > 0 && expect_tx && expect_n >= 3
                    && ltoudp_inbox_pick_reply(expect_tx, expect_n) >= 0) {
                    ltoudp_last_collect_reason = "reply";
                    ltoudp_last_reply_host_us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - wall0).count();
                    break;
                }
                if (timeout_ms <= 0) break;
                if (ltg1_reply) {
                    const auto used = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - wall0).count();
                    wait = (used < timeout_ms) ? (int)(timeout_ms - used) : 0;
                    if (wait > 0) continue;
                    break;
                }
            }
            const uint64_t c1 = ltoudp_now_14m();
            const uint64_t y1 = ltoudp_guest_cycles();
            const auto wall_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - wall0).count();
            if (timeout_ms > 0 && ltg1_reply
                && strcmp(ltoudp_last_collect_reason, "timeout") == 0)
                ltoudp_last_collect_reason = "peer-fault";
            if (timeout_ms > 0 || recvd)
                ltoudp_trace("collect-end recvd=%zu inbox=%zu timeout=%d reason=%s reply_host_us=%lld c14m=%llu cyc=%llu dc14m=%lld dcyc=%lld wall_us=%lld frozen=%d",
                    recvd, ltoudp_inbox.size(), timeout_ms,
                    ltoudp_last_collect_reason,
                    (long long)ltoudp_last_reply_host_us,
                    (unsigned long long)c1, (unsigned long long)y1,
                    (long long)(c1 - c0), (long long)(y1 - y0),
                    (long long)wall_us,
                    (c1 == c0 && y1 == y0) ? 1 : 0);
            if (timeout_ms > 0 && ltg1_reply
                && strcmp(ltoudp_last_collect_reason, "peer-fault") == 0)
                ltoudp_trace("collect-peer-fault type=$%02X n=%zu wall_us=%lld (safety bound, not a collision)",
                    expect_tx && expect_n >= 3 ? (unsigned)expect_tx[2] : 0,
                    expect_n, (long long)wall_us);
            if (timeout_ms > 0) ltoudp_arm_beacon_timer();
        }

        // Index of the inbox frame that answers a just-transmitted LLAP, or -1.
        int ltoudp_inbox_pick_reply(const uint8_t *tx, size_t n) const {
            const uint8_t t = ltoudp_llap_type(tx, n);
            for (size_t i = 0; i < ltoudp_inbox.size(); i++) {
                const auto &f = ltoudp_inbox[i];
                if (f.size() < 3) continue;
                const uint8_t rt = f[2];
                if (t == 0x81 && rt == 0x82) return (int)i;                 // ACK
                if (t == 0x84 && rt == 0x85) return (int)i;                 // CTS
                if (t == 0x85 && ltoudp_is_data_llap(f.data(), f.size()))
                    return (int)i;                                          // data
                if (ltoudp_atp_fn(tx, n) == 1 && ltoudp_atp_fn(f.data(), f.size()) == 2)
                    return (int)i;                                          // TResp
            }
            return -1;
        }

        bool ltoudp_any_rx_on() const {
            for (int ch = 0; ch < SCC_CHANNEL_COUNT; ch++) {
                auto c = (scc_channel_t)ch;
                if (is_sdlc(c) && registers[c].r3_rx_enable) return true;
            }
            return false;
        }

        bool ltoudp_any_ready() const {
            for (int ch = 0; ch < SCC_CHANNEL_COUNT; ch++) {
                if (ltoudp_rx_ready((scc_channel_t)ch)) return true;
            }
            return false;
        }

        void ltoudp_arm_ifg_timer() {
            if (!event_timer || !clock || ltoudp_due.empty()) return;
            uint64_t soonest = ltoudp_due[0].due_14m;
            for (const auto &d : ltoudp_due)
                if (d.due_14m < soonest) soonest = d.due_14m;
            if (ltoudp_ifg_scheduled) event_timer->cancelEvents(ltoudp_ifg_id);
            event_timer->scheduleEvent(soonest, ltoudp_ifg_callback, ltoudp_ifg_id, this,
                "scc8530.ltoudp_ifg_callback", "src/devices/scc8530/Z85C30.hpp");
            ltoudp_ifg_scheduled = true;
        }

        bool ltoudp_schedule_due(const uint8_t *llap, size_t n, uint64_t due_14m,
                                bool solicited = false) {
            if (!llap || n < 3) return false;
            if (!ltoudp_due.empty()) {
                if (!solicited) {
                    ltoudp_trace("schedule-busy n=%zu type=$%02X due=%llu (one frame on the cable)",
                        n, (unsigned)llap[2], (unsigned long long)due_14m);
                    return false;
                }
                // Solicited reply owns the line during the dialog. Park the
                // unsolicited occupant back at the inbox; it runs after.
                for (auto &d : ltoudp_due) {
                    ltoudp_in_t ev(d.llap.data(), d.llap.data() + d.llap.size());
                    ev.due_14m = d.due_14m;
                    ev.has_due = true;
                    ltoudp_inbox.push_back(std::move(ev));
                }
                ltoudp_trace("schedule-preempt n=%zu type=$%02X due=%llu parked=%zu (solicited owns the line)",
                    n, (unsigned)llap[2], (unsigned long long)due_14m,
                    ltoudp_inbox.size());
                ltoudp_due.clear();
            }
            // A new dialog (RTS/ENQ) waits the inter-dialog gap after the
            // previous frame, not just IFG. Piling an RTS on a just-finished
            // Tickle TResp at eof+IFG overruns PacketIRQ (needData still set).
            const uint64_t gap = (!solicited && (llap[2] == 0x84 || llap[2] == 0x81))
                ? LTOUDP_IDG_14M : LTOUDP_IFG_14M;
            const uint64_t idle = ltoudp_line_idle_14m(gap);
            if (!solicited && due_14m < idle) due_14m = idle;
            else if (solicited && due_14m < ltoudp_now_14m()) due_14m = ltoudp_now_14m();
            ltoudp_due.push_back(ltoudp_due_t{ std::vector<uint8_t>(llap, llap + n), due_14m });
            ltoudp_trace("schedule-due n=%zu type=$%02X due=%llu now=%llu eof=%llu",
                n, (unsigned)llap[2], (unsigned long long)due_14m,
                (unsigned long long)ltoudp_now_14m(),
                (unsigned long long)ltoudp_tx_eof_14m);
            ltoudp_arm_ifg_timer();
            return true;
        }

        // Move a solicited inbox reply onto the due queue at now + IFG.
        // Leftover RTS heads of an RTS+data burst after we sent CTS are dropped.
        bool ltoudp_take_solicited_reply(const uint8_t *tx, size_t n) {
            int idx = ltoudp_inbox_pick_reply(tx, n);
            if (idx < 0) return false;
            if (ltoudp_llap_type(tx, n) == 0x85) {
                // CTS is answered by data; discard leftover RTS in front of it.
                while (idx > 0 && ltoudp_inbox[0].size() >= 3 && ltoudp_inbox[0][2] == 0x84) {
                    ltoudp_inbox.erase(ltoudp_inbox.begin());
                    idx--;
                }
            }
            const auto &f = ltoudp_inbox[(size_t)idx];
            const uint64_t eof = ltoudp_tx_eof_14m ? ltoudp_tx_eof_14m : ltoudp_now_14m();
            // Solicited replies are due at eof+IFG. A later stamp on the
            // datagram (peer line-idle / queued SendData) must not delay them.
            const uint64_t due = eof + LTOUDP_IFG_14M;
            if (!ltoudp_schedule_due(f.data(), f.size(), due, /*solicited=*/true))
                return false;
            ltoudp_inbox.erase(ltoudp_inbox.begin() + idx);
            return true;
        }

        // Unsolicited inbox frames wait for an idle, RX-on line, then one IFG.
        // A datagram that arrives while every SDLC receiver is off is lost.
        void ltoudp_schedule_unsolicited() {
            if (ltoudp_inbox.empty()) return;
            // Do not drop the inbox just because RX is off: a solicited
            // reply may already be here, collected during fXmitLAP DisRx,
            // waiting for take_solicited_reply. Unsolicited wait until RX
            // is on and the line is idle, then one IDG (RTS/ENQ) or IFG.
            if (!ltoudp_any_rx_on()) return;
            if (!ltoudp_due.empty()) return;          // one frame on the wire
            if (!ltoudp_any_ready()) return;          // wait for a frame boundary
            auto f = std::move(ltoudp_inbox.front());
            ltoudp_inbox.erase(ltoudp_inbox.begin());
            uint64_t due = f.has_due ? f.due_14m : ltoudp_now_14m();
            ltoudp_schedule_due(f.data(), f.size(), due);
        }

        bool ltoudp_try_inject_any(const uint8_t *llap, size_t n) {
            for (int ch = 0; ch < SCC_CHANNEL_COUNT; ch++) {
                auto c = (scc_channel_t)ch;
                if (!ltoudp_rx_ready(c)) continue;
                if (ltoudp_try_inject(c, llap, n)) return true;
            }
            return false;
        }

        // Deliver every due frame whose guest time has arrived. RX-off at fire
        // loses the frame (as on the real cable). A busy line defers to a later
        // call once ltoudp_rx_ready (still one IFG from when it was scheduled).
        void ltoudp_deliver_due() {
            const uint64_t now = ltoudp_now_14m();
            for (size_t i = 0; i < ltoudp_due.size(); ) {
                if (ltoudp_due[i].due_14m > now) { ++i; continue; }
                const auto &f = ltoudp_due[i].llap;
                if (ltoudp_try_inject_any(f.data(), f.size())) {
                    ltoudp_trace("fire-inject n=%zu type=$%02X dest=$%02X due=%llu eof=%llu rxen=%d",
                        f.size(), f.size() >= 3 ? (unsigned)f[2] : 0,
                        f.size() >= 1 ? (unsigned)f[0] : 0,
                        (unsigned long long)ltoudp_due[i].due_14m,
                        (unsigned long long)ltoudp_tx_eof_14m,
                        ltoudp_any_rx_on() ? 1 : 0);
                    ltoudp_note_atp1("fire-inject");
                    if (f.size() >= 3 && ltoudp_is_data_llap(f.data(), f.size())) {
                        ltoudp_after_data = true;
                        ltoudp_peek_lap("fire-inject");
                    }
                    ltoudp_due.erase(ltoudp_due.begin() + i);
                    continue;
                }
                if (!ltoudp_any_rx_on()) {
                    ltoudp_trace("fire-lost-rxoff n=%zu type=$%02X dest=$%02X due=%llu eof=%llu rxen=0",
                        f.size(), f.size() >= 3 ? (unsigned)f[2] : 0,
                        f.size() >= 1 ? (unsigned)f[0] : 0,
                        (unsigned long long)ltoudp_due[i].due_14m,
                        (unsigned long long)ltoudp_tx_eof_14m);
                    ltoudp_note_atp1("fire-lost-rxoff");
                    ltoudp_due.erase(ltoudp_due.begin() + i);
                    continue;
                }
                ++i; // line busy, try again when the FIFO is idle
            }
            if (ltoudp_due.empty() && !ltoudp_inbox.empty())
                ltoudp_schedule_unsolicited();
        }


        // Send one completed LLAP frame and, if it expects a reply, collect
        // that reply (guest frozen) and schedule it at now + IFG.
        void ltoudp_on_tx_llap(const uint8_t *tx, size_t n) {
            if (!tx || n < 3 || !ltoudp_enabled()) return;
            ltoudp_tx_eof_14m = ltoudp_now_14m();
            if (ltoudp_lockstep && ltoudp_peer_ltg1) {
                // Every guest TX invalidates leftover lookahead. Clamp
                // to eof+IFG when a solicited reply is possible, otherwise
                // eof+IDG (the peer's minimum reaction time).
                const uint64_t cap = ltoudp_tx_eof_14m
                    + (ltoudp_expects_reply(tx, n) ? LTOUDP_IFG_14M
                                                   : LTOUDP_IDG_14M);
                ltoudp_clamp_14m = cap;
                if (cap < ltoudp_grant_14m) ltoudp_grant_14m = cap;
            }
            ltoudp_trace("tx-eof type=$%02X n=%zu dest=$%02X src=$%02X eof=%llu expect=%d",
                (unsigned)tx[2], n,
                (unsigned)tx[0], (unsigned)tx[1],
                (unsigned long long)ltoudp_tx_eof_14m,
                ltoudp_expects_reply(tx, n) ? 1 : 0);
            if (tx[2] == 0x85) ltoudp_after_cts = true;
            ltoudp_send_llap_guest(tx, n, ltoudp_tx_eof_14m, 0, ltoudp_peer_ltg1);
            if (ltoudp_now_14m() - ltoudp_last_beacon_14m >= LTOUDP_BEACON_14M)
                ltoudp_send_beacon();
            ltoudp_arm_beacon_timer();
            if (ltoudp_expects_reply(tx, n)) {
                if (ltoudp_inbox_pick_reply(tx, n) < 0) {
                    if (ltoudp_peer_ltg1)
                        ltoudp_collect(LTOUDP_LTG1_SAFETY_MS, tx, n, /*ltg1_reply=*/true);
                    else
                        ltoudp_collect(LTOUDP_REPLY_MS, tx, n, /*ltg1_reply=*/false);
                }
                if (!ltoudp_take_solicited_reply(tx, n))
                    ltoudp_trace("no-answer type=$%02X n=%zu reason=%s",
                        (unsigned)tx[2], n, ltoudp_last_collect_reason);
            } else {
                ltoudp_collect(0);
                ltoudp_schedule_unsolicited();
            }
        }

        // Kept as a name for older call sites: collect, then honour guest-time due.
        void ltoudp_drain(int timeout_ms) {
            ltoudp_collect(timeout_ms);
            ltoudp_schedule_unsolicited();
            ltoudp_deliver_due();
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
            } else if (is_sdlc(channel)) {
                // FORK: SDLC bytes are a LocalTalk frame, not an async host UART stream.
                if (sdlc_tx[channel].size() < 605) sdlc_tx[channel].push_back(data);
            } else {
                if (g_scc_capture[channel].enabled) g_scc_capture[channel].byte(data);
                a2events::byte(channel==SCC_CHANNEL_A?"scc-a":"scc-b",data);
                if (serial_devices[channel] != nullptr)
                    serial_devices[channel]->q_host.send(SerialMessage{MESSAGE_DATA, data});
            }

            // FORK: a character waiting in the buffer moves into the now-idle shift register.
            if (!registers[channel].r0_tx_buffer_empty) {
                tx_load_shift(channel);
            } else {
                // FORK: the transmitter underran. RR0 D6 (Tx Underrun/EOM) is set when the
                // shift register empties with no character waiting (Z8530 RR0). ATP1
                // XmitEnd UndWait polls this after ResetUnd (lap_includes.aii:2812-2815).
                registers[channel].r0_tx_underrun_eom = 1;
                // FORK: a complete SDLC/LLAP frame is one LToUDP datagram.
                // If the protocol answers this frame, freeze the guest only
                // to collect the peer's datagram, then deliver it at TX EOF
                // + IFG (~200 us) in guest time, inside TIFGap. Do not inject
                // here. A host timeout with no datagram is a quiet line.
                if (is_sdlc(channel) && sdlc_tx[channel].size() >= 3 && ltoudp_enabled())
                    ltoudp_on_tx_llap(sdlc_tx[channel].data(), sdlc_tx[channel].size());
                sdlc_tx[channel].clear();
            }

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

        static void ltoudp_ifg_callback(uint64_t instanceID, void* userData) {
            (void)instanceID;
            Z85C30* scc = static_cast<Z85C30*>(userData);
            scc->ltoudp_ifg_scheduled = false;
            scc->ltoudp_deliver_due();
            scc->ltoudp_arm_ifg_timer();
        }

};
