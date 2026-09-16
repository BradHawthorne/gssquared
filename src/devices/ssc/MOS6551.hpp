#pragma once

#include <cstdint>
#include <cstdio>

#include "NClock.hpp"
#include "serial_devices/SerialDevice.hpp"
#include "serial_devices/host/HostSerial.hpp"
#include "util/DebugFormatter.hpp"
#include "util/EventTimer.hpp"
#include "util/InterruptController.hpp"
#include "device_irq_id.hpp"

constexpr bool ACIA6551_DEBUG = false;

/* 6551 on the SSC is clocked by a 1.8432 MHz crystal. */
constexpr uint64_t ACIA_XTAL_HZ = 1'843'200;

/* Emulator master clock (14M). */
constexpr uint64_t ACIA_MASTER_CLOCK = 14'318'180;

/**
 * MOS/Synertek 6551 ACIA — register model from the datasheet and Apple SSC docs.
 * TX/RX are paced on the emu thread via EventTimer; SerialDevice backends run
 * on a child thread and communicate only through SPSC queues.
 *
 * FORK: THE DATASHEET'S BEHAVIOUR (fidelity study, serial step 7, [Rockwell R6551/R65C51]):
 *  - A1 A programmed reset (a write to the status register) clears command bits 4-0 and
 *       the overrun bit only; the control register is untouched. It did a full reset.
 *  - A2 The transmit interrupt fires when a byte moves into the shift register, when it is
 *       enabled with the transmitter empty, and -- if the CPU does not reload -- again at the
 *       character rate while the transmitter idles. It fired once per character.
 *  - A3 DCD and DSR come from the attached device (MESSAGE_MODEM_STATUS); a change
 *       interrupts at once and the status bits hold the new level until the status register
 *       is read. CTS deasserted lets the character in flight finish, shows TDRE as not empty
 *       and raises no transmit interrupt. Echo mode (command bit 4 with bits 3-2 = 00) sends
 *       received characters straight back. Break (bits 3-2 = 11) is sent to the device.
 *  - A4 Control bit 4 selects the RECEIVER's clock only; the transmitter always uses the
 *       baud generator. Bit 4 = 0 used to untime both.
 *  - A5 Stop bits: 1.5 for 5 data bits without parity, 1 for 8 data bits with parity, 2
 *       otherwise, when control bit 7 asks for two.
 *  - A6 The receiver takes host bytes whenever its shift register is idle (at an access, when
 *       a character completes, and on a 1 ms emulated-time poll), so a byte arriving with
 *       RDRF still set is an overrun: the old byte stays, the new one is lost, OE sets. PE, FE
 *       and OE never interrupt and clear on the next data read. The host path could never
 *       overrun, and OE never cleared.
 *  - A7 IRQ is a latch: set by an interrupting event, cleared by reading the status register.
 *       A later, unrelated write used to re-raise it from the standing conditions.
 *  Not changed: a write with the transmitter disabled is still dropped (A8; the datasheet
 *  is silent), and the SSC's MODEM/TERMINAL jumper block is not modelled.
 */
class MOS6551 {
    InterruptController *irq_control = nullptr;
    EventTimer *event_timer = nullptr;
    NClockII *clock = nullptr;
    device_irq_id irq_id = IRQ_SLOT_2;
    uint64_t timer_base_id = 0x65510000ull;
    SerialDevice *device = nullptr;

    /* Hardware DIP SW2-6 gates the IRQ line to the slot; default enabled. */
    bool dip_irq_enabled = true;

    uint8_t status = 0;
    uint8_t command = 0;
    uint8_t control = 0;
    uint8_t rx_data = 0;
    uint8_t tx_data = 0;       /* byte in the transmit shift register */
    uint8_t tx_holding = 0;    /* byte waiting in TDR when shift is busy */
    bool tx_holding_full = false;

    bool tx_in_progress = false;
    bool rx_in_progress = false;
    bool tx_irq_condition = false;   // kept for the v1 snapshot layout; irq_flag is the latch
    // FORK: when the character in each shift register completes (14M), for the snapshot.
    uint64_t tx_due_14m = 0;
    uint64_t rx_due_14m = 0;

    // FORK: the IRQ latch (A7), the modem inputs (A3), and the timers for the continuous
    // receiver (A6) and the idle transmit interrupt (A2).
    bool irq_flag = false;
    bool dcd_in = true, dsr_in = true, cts_in = true;   // asserted
    bool dcd_dsr_latched = false;
    uint8_t rx_byte = 0;                                // the character in the receive shift register
    bool rx_poll_scheduled = false;
    bool tx_idle_scheduled = false;
    uint8_t line_ctl_last = 0xFF;
    uint8_t break_last = 0xFF;
    uint64_t line_params_last = ~0ull;

    float baud_rate = 9600.0f;

    /* Status register bits */
    static constexpr uint8_t ST_PE = 0x01;
    static constexpr uint8_t ST_FE = 0x02;
    static constexpr uint8_t ST_OE = 0x04;
    static constexpr uint8_t ST_RDRF = 0x08;
    static constexpr uint8_t ST_TDRE = 0x10;
    static constexpr uint8_t ST_DCD = 0x20; /* 1 = no carrier */
    static constexpr uint8_t ST_DSR = 0x40; /* 1 = not ready */
    static constexpr uint8_t ST_IRQ = 0x80;

    /* Command register helpers */
    inline bool dtr_ready() const { return (command & 0x01) != 0; }
    inline bool rx_irq_disabled() const { return (command & 0x02) != 0; }
    inline uint8_t tx_ctrl() const { return (command >> 2) & 0x03; }
    inline bool tx_irq_enabled() const { return tx_ctrl() == 0b01; }
    inline bool rts_low() const {
        uint8_t t = tx_ctrl();
        return t == 0b01 || t == 0b10 || t == 0b11;
    }
    inline bool echo_mode() const { return (command & 0x10) != 0 && tx_ctrl() == 0b00; }
    inline bool receiver_enabled() const { return dtr_ready(); }
    inline bool transmitter_enabled() const { return dtr_ready() && tx_ctrl() != 0b00; }

public:
    MOS6551(InterruptController *irq_control, EventTimer *event_timer, NClockII *clock,
            device_irq_id irq_id, uint64_t timer_base_id = 0x65510000ull)
        : irq_control(irq_control),
          event_timer(event_timer),
          clock(clock),
          irq_id(irq_id),
          timer_base_id(timer_base_id) {
        reset();
    }

    void set_device(SerialDevice *dev) {
        device = dev;
        line_params_last = ~0ull;
        push_line_params();
        lines_out_update(true);
        dcd_in = dsr_in = cts_in = true;   // until the device says otherwise
        refresh_dcd_dsr_bits();
        rx_poll_arm();
    }

    void set_dip_irq_enabled(bool enabled) {
        dip_irq_enabled = enabled;
        update_interrupts();
    }

    bool cts_asserted() const { return cts_in; }

    void reset() {
        if (event_timer) {
            event_timer->cancelEvents(timer_base_id + 0); /* TX */
            event_timer->cancelEvents(timer_base_id + 1); /* RX */
            event_timer->cancelEvents(timer_base_id + 3); /* TX idle */
        }
        status = ST_TDRE; /* TDRE set; DCD/DSR from the pins */
        command = 0;
        control = 0;
        rx_data = 0;
        tx_data = 0;
        tx_holding = 0;
        tx_holding_full = false;
        tx_in_progress = false;
        rx_in_progress = false;
        tx_irq_condition = false;
        irq_flag = false;
        dcd_dsr_latched = false;
        tx_idle_scheduled = false;
        refresh_dcd_dsr_bits();
        baud_rate = 0.0f;
        update_baud();
        lines_out_update();
        update_interrupts();
    }

    uint8_t read_data() {
        uint8_t val = rx_data;
        /* A6: RDRF, and the error bits of that character, clear on the data read. */
        status &= (uint8_t)~(ST_RDRF | ST_PE | ST_FE | ST_OE);
        update_interrupts();
        /* Start baud-timed reception of the next queued byte immediately. */
        update_queues();
        if (ACIA6551_DEBUG) {
            printf("6551: READ DATA = %02X\n", val);
        }
        return val;
    }

    void write_data(uint8_t data) {
        update_queues();
        if (ACIA6551_DEBUG) {
            printf("6551: WRITE DATA = %02X (tx_en=%d tdre=%d shift=%d)\n", data,
                   transmitter_enabled(), (status & ST_TDRE) != 0, tx_in_progress);
        }
        if (!transmitter_enabled()) {
            return;
        }
        if (!(status & ST_TDRE)) {
            /* Holding register still full — overrun, drop */
            if (ACIA6551_DEBUG) {
                printf("6551: TX overrun, dropping %02X\n", data);
            }
            return;
        }
        tx_holding = data;
        tx_holding_full = true;
        status &= (uint8_t)~ST_TDRE;
        tx_idle_cancel();
        if (!tx_in_progress) tx_transfer();
        update_interrupts();
    }

    uint8_t read_status() {
        update_queues();
        uint8_t val = (uint8_t)(status & ~ST_IRQ);
        if (irq_flag) val |= ST_IRQ;
        if (!cts_in) val &= (uint8_t)~ST_TDRE;   // A3: CTS high shows the transmitter not empty
        /* A7: reading the status register clears the IRQ latch; the DCD/DSR bits follow the pins again. */
        irq_flag = false;
        dcd_dsr_latched = false;
        refresh_dcd_dsr_bits();
        update_interrupts();
        if (ACIA6551_DEBUG) {
            printf("6551: READ STATUS = %02X\n", val);
        }
        return val;
    }

    void write_reset(uint8_t /*data*/) {
        if (ACIA6551_DEBUG) {
            printf("6551: programmed reset\n");
        }
        /* A1: command bits 4-0 and the overrun bit; control and the rest are kept. */
        command &= 0xE0;
        status &= (uint8_t)~ST_OE;
        tx_idle_cancel();
        lines_out_update();
        push_line_params();
        update_interrupts();
    }

    uint8_t read_command() {
        /* Command is write-only on real silicon; return last written value. */
        return command;
    }

    void write_command(uint8_t data) {
        if (ACIA6551_DEBUG) {
            printf("6551: WRITE COMMAND = %02X\n", data);
        }
        const bool was_tx_irq = tx_irq_enabled() && dtr_ready();
        command = data;
        push_line_params();
        lines_out_update();
        /* A2: enabling the transmit interrupt with the transmitter empty interrupts at once. */
        if (!was_tx_irq && tx_irq_enabled() && dtr_ready()) tx_idle_check(true);
        if (!tx_irq_enabled()) tx_idle_cancel();
        rx_poll_arm();
        update_interrupts();
    }

    uint8_t read_control() {
        /* Control is write-only on real silicon; return last written value. */
        return control;
    }

    void write_control(uint8_t data) {
        if (ACIA6551_DEBUG) {
            printf("6551: WRITE CONTROL = %02X\n", data);
        }
        control = data;
        update_baud();
        push_line_params();
    }

    // FORK: snapshot (M5). The registers, both shift registers, the holding byte and
    // how far each character in flight is from completing. Bytes still queued in the
    // host serial device are not part of the chip and are not saved. Version 2 adds the
    // IRQ latch and the modem inputs.
    void A2GSPU_snapshot(FILE *f) const {
        const uint8_t b[10] = { status, command, control, rx_data, tx_data, tx_holding,
                                static_cast<uint8_t>(tx_holding_full ? 1 : 0), static_cast<uint8_t>(tx_in_progress ? 1 : 0),
                                static_cast<uint8_t>(rx_in_progress ? 1 : 0), static_cast<uint8_t>(irq_flag ? 1 : 0) };
        fwrite(b, 1, 10, f);
        const uint64_t now = clock ? clock->get_c14m() : 0;
        const uint64_t q[2] = { tx_in_progress && tx_due_14m > now ? tx_due_14m - now : 0,
                                rx_in_progress && rx_due_14m > now ? rx_due_14m - now : 0 };
        fwrite(q, 8, 2, f);
        const uint8_t v2[6] = { static_cast<uint8_t>(dcd_in), static_cast<uint8_t>(dsr_in), static_cast<uint8_t>(cts_in),
                                static_cast<uint8_t>(dcd_dsr_latched), rx_byte, 0 };
        fwrite(v2, 1, 6, f);
    }
    static constexpr int A2GSPU_RECORD_V1 = 26;
    static constexpr int A2GSPU_RECORD_V2 = 32;

    bool A2GSPU_restore(FILE *f, int version = 2) {
        uint8_t b[10];
        uint64_t q[2];
        if (fread(b, 1, 10, f) != 10 || fread(q, 8, 2, f) != 2) return false;
        uint8_t v2[6] = { 1, 1, 1, 0, 0, 0 };
        if (version >= 2 && fread(v2, 1, 6, f) != 6) return false;
        if (event_timer) {
            event_timer->cancelEvents(timer_base_id + 0);
            event_timer->cancelEvents(timer_base_id + 1);
            event_timer->cancelEvents(timer_base_id + 3);
        }
        tx_idle_scheduled = false;
        status = b[0]; command = b[1]; control = b[2]; rx_data = b[3]; tx_data = b[4]; tx_holding = b[5];
        tx_holding_full = b[6] != 0; tx_in_progress = b[7] != 0; rx_in_progress = b[8] != 0;
        irq_flag = version >= 2 ? b[9] != 0 : (status & ST_IRQ) != 0;
        dcd_in = v2[0] != 0; dsr_in = v2[1] != 0; cts_in = v2[2] != 0; dcd_dsr_latched = v2[3] != 0;
        rx_byte = version >= 2 ? v2[4] : rx_data;
        update_baud();
        line_params_last = ~0ull;
        push_line_params();
        lines_out_update(true);
        const uint64_t now = clock ? clock->get_c14m() : 0;
        if (tx_in_progress) {
            if (q[0] && event_timer && clock) {
                tx_due_14m = now + q[0];
                event_timer->scheduleEvent(tx_due_14m, tx_complete_callback, timer_base_id + 0, this);
            } else {
                tx_complete();
            }
        }
        if (rx_in_progress) {
            if (q[1] && event_timer && clock) {
                rx_due_14m = now + q[1];
                event_timer->scheduleEvent(rx_due_14m, rx_complete_callback, timer_base_id + 1, this);
            } else {
                rx_complete();
            }
        }
        tx_idle_check(false);
        rx_poll_arm();
        update_interrupts();
        return true;
    }

    void debug_output(DebugFormatter *df) {
        df->addLine("6551 Status:  %02X  Command: %02X  Control: %02X", status | (irq_flag ? ST_IRQ : 0), command, control);
        df->addLine("6551 RX: %02X  TX: %02X  baud: %.2f  bits/char: %.1f", rx_data, tx_data, baud_rate,
                    (double)half_bits_per_char() / 2.0);
        df->addLine("6551 tx_busy=%d rx_busy=%d dip_irq=%d irq_latch=%d", tx_in_progress, rx_in_progress,
                    dip_irq_enabled, irq_flag);
        df->addLine("6551 lines in: DCD %d DSR %d CTS %d%s | out: DTR %d RTS %d break %d | echo %d rx clock %s",
                    dcd_in, dsr_in, cts_in, dcd_dsr_latched ? " (latched)" : "", dtr_ready(), rts_low(),
                    tx_ctrl() == 0b11, echo_mode(), (control & 0x10) ? "baud generator" : "external (untimed)");
    }

private:
    // Baud generator divisors for the 1.8432 MHz crystal (16x clock): rate = 1843200 / (16 * div).
    static constexpr uint16_t baud_div[16] = {
        0,    /* 0000 external 16x */
        2304, /* 50 */
        1536, /* 75 */
        1047, /* 109.92 */
        857,  /* 134.58 */
        768,  /* 150 */
        384,  /* 300 */
        192,  /* 600 */
        96,   /* 1200 */
        64,   /* 1800 */
        48,   /* 2400 */
        32,   /* 3600 */
        24,   /* 4800 */
        16,   /* 7200 */
        12,   /* 9600 */
        6,    /* 19200 */
    };

    void update_baud() {
        const uint8_t sel = control & 0x0F;
        baud_rate = baud_div[sel] ? (float)((double)ACIA_XTAL_HZ / (16.0 * baud_div[sel])) : 0.0f;
    }

    void push_line_params() {
        if (device == nullptr) {
            return;
        }
        host_serial_line_t line;
        line.baud = (baud_rate <= 0.0f) ? 9600 : static_cast<uint32_t>(baud_rate + 0.5f);
        switch ((control >> 5) & 0x03) {
            case 0: line.data_bits = 8; break;
            case 1: line.data_bits = 7; break;
            case 2: line.data_bits = 6; break;
            default: line.data_bits = 5; break;
        }
        if ((command & 0x20) == 0) {
            line.parity = HOST_SERIAL_PARITY_NONE;
        } else {
            switch ((command >> 6) & 0x03) {
                case 0: line.parity = HOST_SERIAL_PARITY_ODD; break;
                case 1: line.parity = HOST_SERIAL_PARITY_EVEN; break;
                case 2: line.parity = HOST_SERIAL_PARITY_MARK; break;
                default: line.parity = HOST_SERIAL_PARITY_SPACE; break;
            }
        }
        const int half = stop_half_bits();
        line.stop_bits = half == 2 ? 1 : (half == 3 ? 15 : 2);
        const uint64_t packed = host_serial_pack_line(line);
        if (packed == line_params_last) return;   // FORK: only when something changed
        if (device->q_host.send(SerialMessage{MESSAGE_LINE, packed})) line_params_last = packed;
    }

    // FORK: DTR, RTS and break to the device, when they change (A3).
    void lines_out_update(bool force = false) {
        if (device == nullptr) return;
        const uint8_t ctl = (uint8_t)((dtr_ready() ? 1 : 0) | (rts_low() ? 2 : 0));
        if (force || ctl != line_ctl_last) {
            if (device->q_host.send(SerialMessage{MESSAGE_MODEM_CTL, ctl})) line_ctl_last = ctl;
        }
        const uint8_t brk = tx_ctrl() == 0b11 ? 1 : 0;
        if (force || brk != break_last) {
            if (device->q_host.send(SerialMessage{brk ? MESSAGE_BREAK_ON : MESSAGE_BREAK_OFF, 0})) break_last = brk;
        }
    }

    // A5: stop bits in half bits.
    int stop_half_bits() const {
        if (!(control & 0x80)) return 2;
        const int data = 8 - ((control >> 5) & 0x03);
        const bool parity = (command & 0x20) != 0;
        if (data == 5 && !parity) return 3;
        if (data == 8 && parity) return 2;
        return 4;
    }

    int half_bits_per_char() const {
        const int data = 8 - ((control >> 5) & 0x03);
        return 2 * (1 + data + ((command & 0x20) ? 1 : 0)) + stop_half_bits();
    }

    // 14M ticks for one character in each direction; 0 = untimed (external clock).
    uint64_t tx_ticks() const {
        const uint16_t div = baud_div[control & 0x0F];
        if (!div) return 0;
        return (ACIA_MASTER_CLOCK * (uint64_t)half_bits_per_char() * 16ull * div) / (ACIA_XTAL_HZ * 2ull);
    }
    uint64_t rx_ticks() const {
        if (!(control & 0x10)) return 0;   // A4: the receiver's own clock is external
        return tx_ticks();
    }

    void refresh_dcd_dsr_bits() {
        if (dcd_dsr_latched) return;
        status = (uint8_t)((status & ~(ST_DCD | ST_DSR)) | (dcd_in ? 0 : ST_DCD) | (dsr_in ? 0 : ST_DSR));
    }

    // A6: take the next host message when the receive shift register is idle.
    void update_queues() {
        if (device == nullptr) {
            return;
        }
        while (!rx_in_progress) {
            SerialMessage msg = device->q_dev.get();
            if (msg.type == MESSAGE_NONE) return;
            if (msg.type == MESSAGE_DATA) {
                if (receiver_enabled()) schedule_rx_char(static_cast<uint8_t>(msg.data));
                continue;
            }
            if (msg.type == MESSAGE_MODEM_STATUS) {
                const bool dcd = (msg.data & 1) != 0;
                const bool cts = (msg.data & 2) != 0;
                const bool dsr = (msg.data & 8) != 0;
                const bool changed_dd = dcd != dcd_in || dsr != dsr_in;
                const bool cts_rose = cts && !cts_in;
                dcd_in = dcd; dsr_in = dsr; cts_in = cts;
                if (changed_dd) {
                    // A3: DCD/DSR changes interrupt at once; the bits hold the new level until
                    // the status register is read.
                    dcd_dsr_latched = false;
                    refresh_dcd_dsr_bits();
                    dcd_dsr_latched = true;
                    if (dtr_ready() && !rx_irq_disabled()) irq_flag = true;
                }
                if (cts_rose && !tx_in_progress && tx_holding_full) tx_transfer();
                update_interrupts();
            }
        }
    }

    void schedule_rx_char(uint8_t data) {
        rx_byte = data;
        rx_in_progress = true;
        uint64_t cycles = rx_ticks();
        if (cycles > 0 && event_timer && clock) {
            uint64_t when = clock->get_c14m() + cycles;
            rx_due_14m = when;
            event_timer->scheduleEvent(when, rx_complete_callback, timer_base_id + 1, this);
        } else {
            rx_complete();
        }
    }

    // A2: the holding register moves into the idle shift register; TDRE sets and the transmit
    // interrupt fires at the start bit. CTS deasserted holds the byte (A3).
    void tx_transfer() {
        if (!tx_holding_full || tx_in_progress || !cts_in) return;
        tx_holding_full = false;
        tx_data = tx_holding;
        tx_in_progress = true;
        status |= ST_TDRE;
        if (tx_irq_enabled() && dtr_ready()) irq_flag = true;
        uint64_t cycles = tx_ticks();
        if (cycles > 0 && event_timer && clock) {
            uint64_t when = clock->get_c14m() + cycles;
            tx_due_14m = when;
            event_timer->scheduleEvent(when, tx_complete_callback, timer_base_id + 0, this);
        } else {
            tx_complete();
        }
    }

    void tx_complete() {
        tx_in_progress = false;
        if (device != nullptr) {
            device->q_host.send(SerialMessage{MESSAGE_DATA, tx_data});
        }
        if (ACIA6551_DEBUG) {
            printf("6551: TX complete %02X\n", tx_data);
        }
        if (tx_holding_full) tx_transfer();
        else tx_idle_check(false);
        update_interrupts();
    }

    // A2: while the transmitter idles with its interrupt enabled, it interrupts again at the
    // character rate. `now` interrupts immediately (the enable itself).
    void tx_idle_check(bool now) {
        if (!(tx_irq_enabled() && dtr_ready() && (status & ST_TDRE) && !tx_in_progress && !tx_holding_full && cts_in)) return;
        if (now) irq_flag = true;
        if (tx_idle_scheduled || !event_timer || !clock) return;
        const uint64_t ticks = tx_ticks();
        if (!ticks) return;
        tx_idle_scheduled = true;
        event_timer->scheduleEvent(clock->get_c14m() + ticks, tx_idle_callback, timer_base_id + 3, this);
    }
    void tx_idle_cancel() {
        if (tx_idle_scheduled && event_timer) event_timer->cancelEvents(timer_base_id + 3);
        tx_idle_scheduled = false;
    }
    void tx_idle_tick() {
        tx_idle_scheduled = false;
        tx_idle_check(true);
        update_interrupts();
    }

    void rx_complete() {
        rx_in_progress = false;
        if (echo_mode() && device != nullptr) {
            device->q_host.send(SerialMessage{MESSAGE_DATA, rx_byte});   // A3: echo
        }
        if (status & ST_RDRF) {
            status |= ST_OE;   // A6: the old byte stays, this one is lost; OE does not interrupt
        } else {
            rx_data = rx_byte;
            status |= ST_RDRF;
            if (dtr_ready() && !rx_irq_disabled()) irq_flag = true;
        }
        if (ACIA6551_DEBUG) {
            printf("6551: RX complete %02X\n", rx_byte);
        }
        update_interrupts();
        update_queues();   // back to back, at the line rate
    }

    void rx_poll_arm() {
        if (!event_timer || !clock || device == nullptr || rx_poll_scheduled) return;
        rx_poll_scheduled = true;
        event_timer->scheduleEvent(clock->get_c14m() + ACIA_MASTER_CLOCK / 1000, rx_poll_callback, timer_base_id + 2, this);
    }
    void rx_poll() {
        rx_poll_scheduled = false;
        if (device == nullptr) return;
        update_queues();
        rx_poll_arm();
    }

    void update_interrupts() {
        // A7: the line is the latch, gated by DTR (command bit 0 disables every interrupt).
        const bool irq = irq_flag && dip_irq_enabled && dtr_ready();
        if (irq_control) {
            irq_control->set_irq(irq_id, irq);
        }
    }

    static void tx_complete_callback(uint64_t /*instanceID*/, void *userData) {
        static_cast<MOS6551 *>(userData)->tx_complete();
    }

    static void rx_complete_callback(uint64_t /*instanceID*/, void *userData) {
        static_cast<MOS6551 *>(userData)->rx_complete();
    }

    static void rx_poll_callback(uint64_t /*instanceID*/, void *userData) {
        static_cast<MOS6551 *>(userData)->rx_poll();
    }

    static void tx_idle_callback(uint64_t /*instanceID*/, void *userData) {
        static_cast<MOS6551 *>(userData)->tx_idle_tick();
    }
};
