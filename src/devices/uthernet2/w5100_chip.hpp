#pragma once

#include "net_worker.hpp"
#include "w5100.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace u2 {

struct SocketState {
    uint16_t transmitBase = 0;
    uint16_t transmitSize = 0;
    uint16_t receiveBase = 0;
    uint16_t receiveSize = 0;
    uint16_t registerAddress = 0;
    uint16_t sn_rx_wr = 0;
    uint16_t sn_rx_rsr = 0;
    uint8_t status = W5100_SN_SR_CLOSED;
    uint8_t headerSize = 0;
    std::vector<uint8_t> rx_pending;
    // FORK: bumped by every OPEN, CLOSE, DISCON and reset. Host events carry the
    // generation they were produced for; an older one is discarded, so a late
    // "connected" or "closed" cannot land on a socket the guest has since replaced.
    uint32_t gen = 0;
};

/*
 * The W5100 on the Uthernet II -- the FUSED model (Docs/UPSTREAM_SYNC.md).
 *
 * Base: upstream's register/memory model (adapted from AppleWin) and its libslirp +
 * host-socket worker. From the fork's earlier chip: Sn_IR with every interrupt source
 * and IR mirroring it, commands honoured only in the states the datasheet allows,
 * CLOSE_WAIT on a remote close, TIMEOUT on a refused connect or send, a LISTEN bound
 * synchronously so a failure shows in the same register write, the MACRAW socket-0
 * rule, and a loud report of every command it does not emulate.
 *
 * EMULATED TIME: host network events enter the machine only in frame_tick(), at the
 * video-frame boundary -- never from inside a register read -- so the host's network
 * timing changes the machine at defined points that the u2.state records stamp.
 */
class W5100Chip {
public:
    explicit W5100Chip(int slot = 0);
    ~W5100Chip();

    bool start();
    void stop();
    void reset(bool power_cycle);

    uint8_t io_read(uint8_t loc);
    void io_write(uint8_t loc, uint8_t value);

    /** FORK: apply pending host network events. Called once per video frame. */
    void frame_tick();

    /** FORK: the byte a register read would return, with no side effect at all. */
    uint8_t peek(uint16_t address) const;
    uint8_t mode() const { return mode_; }
    uint16_t address_latch() const { return data_addr_; }
    int slot() const { return slot_; }

    /** FORK: devstate / debug view lines. */
    void describe(std::vector<std::string> &out) const;

    /** FORK: snapshot block (M5). Host connections cannot be saved; see restore(). */
    void A2GSPU_snapshot(FILE *f) const;
    bool A2GSPU_restore(FILE *f, int *lost_connections);

private:
    void set_tx_sizes(uint8_t value);
    void set_rx_sizes(uint8_t value);
    void reset_rxtx(size_t i);
    void update_rsr(size_t i);
    void set_header_size(size_t i);

    uint16_t tx_data_size(size_t i) const;
    uint8_t tx_free_reg(size_t i, size_t shift) const;
    uint8_t rx_size_reg(size_t i, size_t shift) const;

    uint8_t read_socket_reg(uint16_t address) const;
    void write_socket_reg(uint16_t address, uint8_t value);
    uint8_t read_at(uint16_t address) const;
    void write_at(uint16_t address, uint8_t value);
    void write_mode(uint8_t value);
    void auto_increment();
    uint8_t read_value();
    void write_value(uint8_t value);

    void handle_command(size_t i, uint8_t cmd);
    const char *open_socket(size_t i);
    const char *listen_socket(size_t i);
    bool send_data(size_t i);
    void close_host(size_t i, NetMsg how);

    void raise_ir(size_t i, uint8_t bits);

    void write_rx_byte(size_t i, uint8_t v);
    void write_rx_be16(size_t i, uint16_t v);
    void write_rx_data(size_t i, const uint8_t *data, size_t len);
    bool room_for(size_t i, size_t len) const;
    size_t rx_free(size_t i) const;
    size_t flush_tcp_pending(size_t i);
    void ingest_tcp(size_t i, const uint8_t *data, size_t len);

    void apply_event(const NetEvent &ev);
    void ingest_macraw(const NetEvent &ev);

    std::vector<uint8_t> memory_;
    std::array<SocketState, U2_NUM_SOCKETS> sockets_{};
    uint8_t mode_ = W5100_MR_IND;
    uint16_t data_addr_ = 0;
    int slot_ = 0;
    NetWorker worker_;
};

}  // namespace u2
