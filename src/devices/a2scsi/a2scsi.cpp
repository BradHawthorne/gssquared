/*
 *   Copyright (c) 2025-2026 Jawaid Bazyar
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// FORK (2026-11-19): Apple II SCSI Card (670-0437, ROM 341-0437-A).
//
// Sources this follows, so the card can be checked against MAME byte for byte:
//  - card glue: MAME src/devices/bus/a2bus/a2scsi.cpp (C0n0-7 5380 registers, C0n8 pseudo-DMA
//    with DACK, C0n9 own ID $80, C0nA ROM/RAM bank, C0nB 5380 reset, C0nC block mode,
//    C0nD pseudo-DMA mode, C0nE bit 7 DRQ; CnXX = ROM page 0; C800-CBFF = 8 KB RAM in
//    1 KB banks or the DMA port in block mode; CC00-CFFF = 1 KB ROM bank). The 5380 IRQ
//    is not wired on the card, as in MAME.
//  - NCR 5380 initiator: MAME src/devices/machine/ncr5380.cpp (register semantics,
//    arbitration, DMA receive/send state machine, phase-mismatch and disconnect rules).
//    Target mode is not modelled (the card firmware never uses it).
//  - the disk: MAME src/devices/bus/nscsi/hd.cpp and machine/nscsi_hle.cpp (selection,
//    MESSAGE OUT while ATN, IDENTIFY LUN, command lengths by group, INQUIRY, MODE SENSE(6)
//    pages, READ CAPACITY, READ/WRITE(6/10), REQUEST SENSE, sense format, STATUS + COMMAND
//    COMPLETE + bus free).
// Timing is not modelled: every bus reaction happens at once, inside the register access
// that caused it.
// Differences from MAME, all deliberate:
//  - the target answers selection only while an image is mounted (an empty card = an
//    empty bus, selection times out);
//  - WRITE(6/10) does not first write a stale buffer to the start block (MAME's
//    image->write range probe), FORMAT UNIT writes nothing (MAME writes block
//    cyl*head*sector), and blocks past the end of the image are not written;
//  - READ CAPACITY and the mode-page block count use the image's own block count
//    (MAME uses its guessed CHS product, which can round a size up).
// The ROM is not part of the fork: copy 341-0437-a.bin (16,384 bytes) to
// resources/roms/cards/a2scsi/341-0437-a.bin.

#include <algorithm>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "gs2.hpp"
#include "cpu.hpp"
#include "mmus/mmu_ii.hpp"
#include "devices/a2scsi/a2scsi.hpp"
#include "util/media.hpp"
#include "util/ResourceFile.hpp"
#include "util/mount.hpp"
#include "util/StorageDevice.hpp"

namespace {

// Off by default: A2SCSI_LOG=<file> writes one line per register access and bus event.
FILE *g_log = nullptr;
void tlog(const char *fmt, ...) {
    if (!g_log) return;
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fflush(g_log);
}

// bus control lines (phase encoding as nscsi: I/O=1, C/D=2, MSG=4)
enum : uint32_t {
    S_IO = 0x001, S_CD = 0x002, S_MSG = 0x004, S_PHASE = 0x007,
    S_REQ = 0x008, S_ACK = 0x010, S_BSY = 0x020, S_SEL = 0x040, S_ATN = 0x080, S_RST = 0x100,
};
enum : uint8_t { PH_DATA_OUT = 0, PH_DATA_IN = 1, PH_COMMAND = 2, PH_STATUS = 3, PH_MSG_OUT = 6, PH_MSG_IN = 7 };

// 5380 register bits (MAME ncr5380.h)
enum : uint8_t {
    IC_RST = 0x80, IC_AIP = 0x40, IC_LA = 0x20, IC_ACK = 0x10, IC_BSY = 0x08, IC_SEL = 0x04,
    IC_ATN = 0x02, IC_DBUS = 0x01, IC_PHASE = 0x9e, IC_WRITE = 0x9f,
    MODE_BSYIRQ = 0x04, MODE_DMA = 0x02, MODE_ARBITRATE = 0x01,
    TC_IO = 0x01, TC_PHASE = 0x07,
    BAS_ENDOFDMA = 0x80, BAS_DMAREQUEST = 0x40, BAS_PARITYERROR = 0x20, BAS_IRQACTIVE = 0x10,
    BAS_PHASEMATCH = 0x08, BAS_BUSYERROR = 0x04, BAS_ATN = 0x02, BAS_ACK = 0x01,
    ST_RST = 0x80, ST_BSY = 0x40, ST_REQ = 0x20, ST_MSG = 0x10, ST_CD = 0x08, ST_IO = 0x04, ST_SEL = 0x02,
};

enum : uint8_t { SS_GOOD = 0x00, SS_CHECK_CONDITION = 0x02 };
enum : uint8_t { SK_NO_SENSE = 0x0, SK_ILLEGAL_REQUEST = 0x5 };

class A2Scsi : public StorageDevice {
public:
    // ---- card ----
    uint8_t rom[0x4000];
    uint8_t ram[0x2000];
    uint32_t rambank = 0, rombank = 0;
    bool block_mode = false;

    // ---- bus: what each side drives ----
    uint32_t init_ctrl = 0, targ_ctrl = 0;
    uint8_t init_data = 0, targ_data = 0;
    uint32_t ctrl() const { return init_ctrl | targ_ctrl; }
    uint8_t data() const { return init_data | targ_data; }

    // ---- 5380 ----
    enum dma_state_t { D_IDLE, D_IN_REQ, D_IN_ACK, D_OUT_DRQ, D_OUT_WAIT, D_OUT_REQ, D_OUT_ACK };
    uint8_t odata = 0, icmd = 0, mode = 0, tcmd = 0, bas = 0, idata = 0, selen = 0;
    dma_state_t dstate = D_IDLE;
    bool dma_kick = false;      // the state machine may step (MAME: state timer armed)
    bool drq = false, irq = false, rst_out = false;
    uint32_t seen_ctrl = 0;     // last bus control the chip reacted to (MAME m_scsi_ctrl)

    // ---- target (one disk, SCSI ID 6 by default) ----
    int target_id = 6;
    FILE *fp = nullptr;
    std::string filename;
    uint64_t data_offset = 0;
    uint32_t block_count = 0;
    bool write_protected = false;
    bool modified = false;
    uint16_t cyl = 0, heads = 0, sectors = 0;

    enum targ_state_t { T_IDLE, T_SEL_WAIT, T_XFER };
    targ_state_t tstate = T_IDLE;
    uint8_t tphase = 0;
    bool treq = false, twait_ack0 = false;
    std::vector<uint8_t> tbuf;
    size_t tpos = 0, tlen = 0;
    uint8_t cmd[32];
    size_t cmdlen = 0;
    std::vector<uint8_t> msgs;
    uint8_t identify = 0;
    uint8_t sense[18];
    uint8_t status_byte = 0;
    bool out_is_write = false;
    uint32_t out_lba = 0;

    A2Scsi() {
        memset(rom, 0xff, sizeof(rom));
        memset(ram, 0, sizeof(ram));
        set_sense(SK_NO_SENSE, 0, 0);
    }

    // ================= storage =================
    bool mount(storage_key_t key, std::vector<media_descriptor *> media_list) override {
        if (media_list.empty() || fp) return false;
        media_descriptor *m = media_list[0];
        FILE *f = fopen(m->filename.c_str(), m->write_protected ? "rb" : "r+b");
        if (!f) { std::cerr << "a2scsi: cannot open " << m->filename << std::endl; return false; }
        fseek(f, 0, SEEK_END);
        uint64_t size = (uint64_t)ftell(f);
        // A whole-disk image: the target sees the host file, less a 2MG header. An APM
        // image is not split into partitions here; the driver reads the map itself.
        uint64_t off = 0;
        std::string fn = m->filename;
        std::string ext = fn.size() >= 4 ? fn.substr(fn.size() - 4) : "";
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".2mg") off = m->data_offset;
        fp = f;
        filename = fn;
        data_offset = off;
        block_count = (uint32_t)((size - off) / 512);
        write_protected = m->write_protected;
        modified = false;
        guess_chs();
        return true;
    }
    bool unmount(storage_key_t key) override {
        if (!fp) return false;
        fclose(fp);
        fp = nullptr;
        filename.clear();
        block_count = 0;
        return true;
    }
    bool writeback(storage_key_t key) override {
        // writes go straight to the host file (see write_blocks), so nothing is pending
        if (fp) fflush(fp);
        modified = false;
        return true;
    }
    drive_status_t status(storage_key_t key) override {
        return { fp != nullptr, filename, tstate != T_IDLE, 0, modified, write_protected };
    }

    // MAME util/harddisk.cpp raw-image geometry guess (used for the mode pages only)
    void guess_chs() {
        cyl = heads = sectors = 0;
        for (uint32_t total = block_count; total; total++)
            for (uint32_t s = 63; s > 1; s--)
                if (total % s == 0) {
                    uint32_t th = total / s;
                    for (uint32_t h = 16; h > 1; h--)
                        if (th % h == 0) { cyl = (uint16_t)(th / h); heads = (uint16_t)h; sectors = (uint16_t)s; return; }
                }
    }

    bool read_block(uint32_t lba, uint8_t *dst) {
        if (!fp || lba >= block_count) return false;
        if (fseek(fp, (long)(data_offset + (uint64_t)lba * 512), SEEK_SET) != 0) return false;
        return fread(dst, 1, 512, fp) == 512;
    }
    void write_blocks(uint32_t lba, const uint8_t *src, size_t n) {
        if (!fp || write_protected) return;
        for (size_t i = 0; i < n; i++) {
            if (lba + i >= block_count) break;
            fseek(fp, (long)(data_offset + (uint64_t)(lba + i) * 512), SEEK_SET);
            fwrite(src + i * 512, 1, 512, fp);
        }
        fflush(fp);
        modified = true;
    }

    // ================= target =================
    void set_sense(uint8_t key, uint8_t asc, uint8_t ascq) {
        memset(sense, 0, sizeof(sense));
        sense[0] = 0xf0;            // valid, current error (MAME sense(false, ...))
        sense[2] = key;
        sense[7] = 10;
        sense[12] = asc;
        sense[13] = ascq;
    }
    int get_lun(int def) const { return (identify & 0x80) ? (identify & 7) : def; }

    void t_send(uint8_t phase, uint8_t b) {
        tphase = phase; treq = true;
        targ_data = b;
        targ_ctrl = S_BSY | phase | S_REQ;
    }
    void t_recv(uint8_t phase) {
        tphase = phase; treq = true;
        targ_data = 0;
        targ_ctrl = S_BSY | phase | S_REQ;
    }
    void t_bus_free() {
        tstate = T_IDLE; treq = false; twait_ack0 = false;
        targ_ctrl = 0; targ_data = 0;
    }
    void t_status(uint8_t st) {
        tlog("T status %02x (data %zu bytes)\n", st, tlen);
        status_byte = st;
        t_send(PH_STATUS, st);
    }
    // DATA IN of whatever is in tbuf, then STATUS with status_byte
    void t_data_in() {
        if (tbuf.empty()) { t_status(status_byte); return; }
        tpos = 0; tlen = tbuf.size();
        t_send(PH_DATA_IN, tbuf[0]);
    }
    void t_data_out(size_t n) {
        tbuf.assign(n, 0);
        if (n == 0) { t_status(status_byte); return; }
        tpos = 0; tlen = n;
        t_recv(PH_DATA_OUT);
    }
    void t_check(uint8_t key, uint8_t asc, uint8_t ascq = 0) {
        set_sense(key, asc, ascq);
        t_status(SS_CHECK_CONDITION);
    }

    static size_t cdb_length(uint8_t op, size_t have) {
        switch (op >> 5) {     // nscsi_full_device::scsi_command_done
        case 0: return 6;
        case 1: case 2: return 10;
        case 5: return 12;
        case 7: return 32;
        default: return have;   // groups 3, 4, 6: done after the first byte
        }
    }

    void t_command();
    void log_cmd() {
        if (!g_log) return;
        tlog("T cmd");
        for (size_t i = 0; i < cmdlen; i++) tlog(" %02x", cmd[i]);
        tlog(" (lun %d)\n", get_lun(cmd[1] >> 5));
    }

    // the target's reaction to the bus (nscsi_full_device::step)
    void target_react() {
        uint32_t c = ctrl();
        if (c & S_RST) {
            if (tstate != T_IDLE || targ_ctrl) t_bus_free();
            tstate = T_IDLE; identify = 0;
            return;
        }
        switch (tstate) {
        case T_IDLE:
            if ((c & (S_SEL | S_BSY)) == S_SEL) tlog("T selection data %02x%s\n", data(), fp ? "" : " (no image: no answer)");
            if (fp && (c & (S_SEL | S_BSY)) == S_SEL && (data() & (1 << target_id))) {
                tstate = T_SEL_WAIT;
                targ_ctrl = S_BSY;
            }
            break;
        case T_SEL_WAIT:
            if (c & S_SEL) break;
            tstate = T_XFER;
            msgs.clear(); cmdlen = 0;
            if (c & S_ATN) t_recv(PH_MSG_OUT);
            else t_recv(PH_COMMAND);
            break;
        case T_XFER:
            if (treq && (c & S_ACK)) {
                uint8_t b = data();
                if (tphase == PH_MSG_OUT) msgs.push_back(b);
                else if (tphase == PH_COMMAND) { if (cmdlen < sizeof(cmd)) cmd[cmdlen++] = b; }
                else if (tphase == PH_DATA_OUT) tbuf[tpos] = b;
                treq = false; twait_ack0 = true;
                targ_ctrl &= ~S_REQ;
            } else if (twait_ack0 && !(c & S_ACK)) {
                twait_ack0 = false;
                t_next(c);
            }
            break;
        }
    }

    void t_next(uint32_t c) {
        switch (tphase) {
        case PH_MSG_OUT:
            if (c & S_ATN) { t_recv(PH_MSG_OUT); break; }
            if (!msgs.empty() && (msgs[0] & 0x80)) identify = msgs[0];   // other messages are ignored, as MAME
            t_recv(PH_COMMAND);
            break;
        case PH_COMMAND:
            if (cmdlen >= cdb_length(cmd[0], cmdlen)) { log_cmd(); t_command(); }
            else t_recv(PH_COMMAND);
            break;
        case PH_DATA_IN:
            if (++tpos < tlen) t_send(PH_DATA_IN, tbuf[tpos]);
            else t_status(status_byte);
            break;
        case PH_DATA_OUT:
            if (++tpos < tlen) t_recv(PH_DATA_OUT);
            else {
                if (out_is_write) write_blocks(out_lba, tbuf.data(), tlen / 512);
                t_status(status_byte);
            }
            break;
        case PH_STATUS:
            t_send(PH_MSG_IN, 0x00);   // COMMAND COMPLETE
            break;
        case PH_MSG_IN:
            t_bus_free();
            break;
        }
    }

    // ================= 5380 =================
    void set_irq(bool s) { irq = s; if (s) bas |= BAS_IRQACTIVE; else bas &= ~BAS_IRQACTIVE; }
    void set_drq(bool s) { drq = s; if (s) bas |= BAS_DMAREQUEST; else bas &= ~BAS_DMAREQUEST; }

    void chip_reset() {
        dstate = D_IDLE; dma_kick = false;
        odata = icmd = mode = tcmd = bas = idata = selen = 0;
        seen_ctrl = 0;
        init_data = 0; init_ctrl = 0;
        set_irq(false); set_drq(false);
    }
    void scsi_data_w(uint8_t v) {
        uint32_t c = ctrl();
        if (!(c & S_IO) && (c & S_PHASE) == (tcmd & TC_PHASE)) init_data = v;
    }

    // ncr5380_device::scsi_ctrl_changed
    void chip_ctrl_changed(uint32_t c) {
        bas &= ~BAS_BUSYERROR;
        if ((icmd & IC_DBUS) && !(c & S_RST) && ((c & S_IO) || (c & S_PHASE) != (tcmd & TC_PHASE)))
            init_data = 0;
        if (c & S_RST) {
            // MAME: device_reset() also releases the chip's own RST, so a bus reset is a
            // pulse the target sees once; the base 5380 interrupts on any RST, own included
            chip_reset();
            set_irq(true);
        } else if ((seen_ctrl & S_BSY) && !(c & S_BSY)) {
            icmd &= (IC_RST | IC_AIP);
            init_ctrl = 0;
            if (mode & MODE_DMA) {
                mode &= ~MODE_DMA;
                bas &= ~BAS_ENDOFDMA;
                set_drq(false);
            }
            if (mode & MODE_BSYIRQ) { bas |= BAS_BUSYERROR; set_irq(true); }
            dstate = D_IDLE; dma_kick = false;
            init_data = 0;
        } else if (!(seen_ctrl & S_REQ) && (c & S_REQ) && (mode & MODE_DMA)) {
            if ((c & S_PHASE) == (tcmd & TC_PHASE)) {
                if (dstate != D_IDLE) dma_kick = true;
            } else {
                dstate = D_IDLE; dma_kick = false;
                if (!(tcmd & TC_IO)) set_drq(true);
                set_irq(true);
            }
        }
        seen_ctrl = c;
    }

    // one step of the DMA state machine (ncr5380_device::state_step); false = stalled
    bool chip_step() {
        if (!dma_kick) return false;
        uint32_t c = ctrl();
        switch (dstate) {
        case D_IDLE:
            dma_kick = false;
            return false;
        case D_IN_REQ:
            if (c & S_REQ) {
                if ((c & S_PHASE) == (tcmd & TC_PHASE)) {
                    idata = data();
                    dstate = D_IN_ACK;
                    set_drq(true);
                    init_ctrl |= S_ACK;
                }
                dma_kick = false;      // stall until dma_r
                return true;
            }
            dma_kick = false;          // wait for REQ (chip_ctrl_changed re-kicks)
            return false;
        case D_IN_ACK:
            if (!(c & S_REQ)) {
                dstate = (bas & BAS_ENDOFDMA) ? D_IDLE : D_IN_REQ;
                init_ctrl &= ~S_ACK;
                return true;
            }
            dma_kick = false;
            return false;
        case D_OUT_DRQ:
            dstate = D_OUT_WAIT;
            set_drq(true);
            dma_kick = false;
            return true;
        case D_OUT_WAIT:
            dma_kick = false;
            return false;
        case D_OUT_REQ:
            if ((c & S_REQ) && (c & S_PHASE) == (tcmd & TC_PHASE)) {
                dstate = D_OUT_ACK;
                init_data = odata;
                init_ctrl |= S_ACK;
                return true;
            }
            dma_kick = false;
            return false;
        case D_OUT_ACK:
            if (!(c & S_REQ)) {
                dstate = (bas & BAS_ENDOFDMA) ? D_IDLE : D_OUT_DRQ;
                init_data = 0;
                init_ctrl &= ~S_ACK;
                return true;
            }
            dma_kick = false;
            return false;
        }
        return false;
    }

    // Run the bus until nothing changes. As MAME's nscsi bus (regen_ctrl), a change of the
    // control lines notifies every device except the one that made it: the target reacts to
    // the chip's changes, the chip (scsi_ctrl_changed) to the target's. Data-line changes
    // notify nobody.
    uint32_t init_snap = 0, targ_snap = 0, bus_snap = 0;
    bool in_settle = false;
    void settle() {
        if (in_settle) return;
        in_settle = true;
        int i;
        for (i = 0; i < 100000; i++) {
            uint32_t bus = ctrl();
            bool by_chip = init_ctrl != init_snap, by_targ = targ_ctrl != targ_snap;
            bool changed = bus != bus_snap;
            init_snap = init_ctrl; targ_snap = targ_ctrl; bus_snap = bus;
            bool any = false;
            if (changed && by_chip) { target_react(); any = true; }
            if (changed && by_targ) { chip_ctrl_changed(ctrl()); any = true; }
            if (chip_step()) any = true;
            if (!any && init_ctrl == init_snap && targ_ctrl == targ_snap) break;
        }
        if (i == 100000) std::cerr << "a2scsi: bus did not settle" << std::endl;
        in_settle = false;
    }

    // ---- registers ----
    uint8_t reg_r(int r) {
        switch (r) {
        case 0: return data();
        case 1: return icmd;
        case 2: return mode;
        case 3: return tcmd;
        case 4: {
            uint32_t c = ctrl();
            return (c & S_RST ? ST_RST : 0) | (c & S_BSY ? ST_BSY : 0) | (c & S_REQ ? ST_REQ : 0) |
                   (c & S_MSG ? ST_MSG : 0) | (c & S_CD ? ST_CD : 0) | (c & S_IO ? ST_IO : 0) |
                   (c & S_SEL ? ST_SEL : 0);
        }
        case 5: {
            uint32_t c = ctrl();
            return bas | (((c & S_PHASE) == (tcmd & TC_PHASE)) ? BAS_PHASEMATCH : 0) |
                   (c & S_ATN ? BAS_ATN : 0) | (c & S_ACK ? BAS_ACK : 0);
        }
        case 6: return idata;
        default:
            bas &= ~(BAS_PARITYERROR | BAS_BUSYERROR);
            set_irq(false);
            return 0;
        }
    }

    void reg_w(int r, uint8_t v) {
        switch (r) {
        case 0:
            if (icmd & IC_DBUS) scsi_data_w(v);
            odata = v;
            break;
        case 1:
            if (!(v & IC_RST)) {
                rst_out = false;
                if ((v ^ icmd) & IC_DBUS) scsi_data_w((v & IC_DBUS) ? odata : 0);
                if ((v & IC_PHASE) ^ (icmd & IC_PHASE)) {
                    uint32_t keep = init_ctrl & ~(S_RST | S_ACK | S_BSY | S_SEL | S_ATN);
                    init_ctrl = keep | (v & IC_ACK ? S_ACK : 0) | (v & IC_BSY ? S_BSY : 0) |
                                (v & IC_SEL ? S_SEL : 0) | (v & IC_ATN ? S_ATN : 0);
                    settle();      // MAME: the target reacts inside ctrl_w, before m_icmd is set
                }
            } else {
                chip_reset();
                rst_out = true;
                init_ctrl = S_RST;
                settle();
            }
            icmd = (icmd & ~IC_WRITE) | (v & IC_WRITE);   // after the reset, as MAME: reads back $80
            break;
        case 2:
            if (!(v & MODE_BSYIRQ)) bas &= ~BAS_BUSYERROR;
            if ((mode & MODE_DMA) && !(v & MODE_DMA)) {
                dstate = D_IDLE; dma_kick = false;
                bas &= ~BAS_ENDOFDMA;
                set_drq(false);
                init_ctrl &= ~S_ACK;
            }
            if ((mode ^ v) & MODE_ARBITRATE) {
                if (v & MODE_ARBITRATE) {
                    icmd &= ~IC_LA;
                    // ARB_BUS_FREE -> ARB_START -> ARB_EVALUATE, all at once; nothing else
                    // arbitrates on this bus, so it is won whenever the bus is free
                    if (!(ctrl() & (S_SEL | S_BSY | S_RST))) {
                        icmd |= IC_AIP | IC_BSY;
                        init_data = odata;
                        init_ctrl |= S_BSY;
                    }
                } else {
                    icmd &= ~(IC_AIP | IC_LA);
                }
            }
            mode = v;
            break;
        case 3:
            tcmd = v;
            break;
        case 4:
            selen = v;
            break;
        case 5:
            if (mode & MODE_DMA) { dstate = D_OUT_DRQ; dma_kick = true; }
            break;
        case 6:
            break;          // start DMA target receive: target mode only
        case 7:
            if (mode & MODE_DMA) { dstate = D_IN_REQ; dma_kick = true; }
            break;
        }
        settle();
    }

    uint8_t dma_read() {
        // MAME dma_r: returns the latched byte; the next one is latched after it
        uint8_t v = idata;
        set_drq(false);
        if (mode & MODE_DMA) { dma_kick = true; settle(); }
        return v;
    }
    void dma_w(uint8_t v) {
        set_drq(false);
        if (mode & MODE_DMA) {
            odata = v;
            if (dstate == D_OUT_WAIT) dstate = D_OUT_REQ;
            dma_kick = true;
            settle();
        }
    }

    // ---- card I/O ----
    uint8_t c0n_r(int off) {
        uint8_t v = c0n_r_(off);
        if (g_log && off != 8) tlog("R C0n%X %02x\n", off, v);
        return v;
    }
    uint8_t c0n_r_(int off) {
        switch (off) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: return reg_r(off);
        case 8: return dma_read();
        case 9: return 0x80;
        case 0xe: return drq ? 0x80 : 0x00;
        default: return 0xff;
        }
    }
    void c0n_w(int off, uint8_t v) {
        if (g_log && off != 8) tlog("W C0n%X %02x\n", off, v);
        switch (off) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: reg_w(off, v); break;
        case 8: dma_w(v); break;
        case 0xa:
            rambank = ((v >> 4) & 7) * 0x400;
            rombank = (v & 0xf) * 0x400;
            block_mode = false;
            break;
        case 0xb:
            chip_reset();
            block_mode = false;
            settle();
            break;
        case 0xc: block_mode = true; break;
        case 0xd: block_mode = false; break;
        default: break;
        }
    }
    uint8_t c800_r(uint16_t off) {
        if (off < 0x400) return block_mode ? dma_read() : ram[off + rambank];
        return rom[((off - 0x400) + rombank) & 0x3fff];
    }
    void c800_w(uint16_t off, uint8_t v) {
        if (off < 0x400) {
            if (block_mode) dma_w(v);
            else ram[off + rambank] = v;
        }
    }
};

void A2Scsi::t_command() {
    uint8_t op = cmd[0];
    status_byte = SS_GOOD;
    out_is_write = false;
    auto u16 = [&](int i) { return (uint32_t)((cmd[i] << 8) | cmd[i + 1]); };
    auto u32 = [&](int i) { return (uint32_t)((cmd[i] << 24) | (cmd[i + 1] << 16) | (cmd[i + 2] << 8) | cmd[i + 3]); };
    switch (op) {
    case 0x00:  // TEST UNIT READY
        if (!get_lun(cmd[1] >> 5)) t_status(SS_GOOD);
        else t_check(SK_ILLEGAL_REQUEST, 0x25);
        break;
    case 0x01:  // REZERO UNIT
        t_status(SS_GOOD);
        break;
    case 0x03: {  // REQUEST SENSE
        if ((cmd[1] & 0x1f) || cmd[2] || cmd[3]) { t_check(SK_ILLEGAL_REQUEST, 0x24); break; }
        size_t n = std::min<size_t>(18, cmd[4]);
        tbuf.assign(sense, sense + n);
        t_data_in();
        break;
    }
    case 0x08: case 0x28: {  // READ(6) / READ(10)
        uint32_t lba, blocks;
        if (op == 0x08) { lba = ((cmd[1] << 16) | (cmd[2] << 8) | cmd[3]) & 0x1fffff; blocks = cmd[4] ? cmd[4] : 256; }
        else { lba = u32(2); blocks = u16(7); }
        std::vector<uint8_t> blk(512);
        if (!read_block(lba, blk.data())) { t_check(SK_ILLEGAL_REQUEST, 0x24); break; }
        std::vector<uint8_t> all((size_t)blocks * 512, 0);
        for (uint32_t i = 0; i < blocks; i++) read_block(lba + i, &all[(size_t)i * 512]);
        tbuf = all;
        t_data_in();
        break;
    }
    case 0x0a: case 0x2a: {  // WRITE(6) / WRITE(10)
        uint32_t lba, blocks;
        if (op == 0x0a) { lba = ((cmd[1] << 16) | (cmd[2] << 8) | cmd[3]) & 0x1fffff; blocks = cmd[4] ? cmd[4] : 256; }
        else { lba = u32(2); blocks = u16(7); }
        if (!fp || lba >= block_count || write_protected) { t_check(SK_ILLEGAL_REQUEST, 0x24); break; }
        out_is_write = true; out_lba = lba;
        t_data_out((size_t)blocks * 512);
        break;
    }
    case 0x12: {  // INQUIRY
        int lun = get_lun(cmd[1] >> 5);
        int size = cmd[4];
        if (cmd[2] != 0) { t_status(SS_GOOD); break; }   // MAME: other pages, no data
        std::vector<uint8_t> b(148, 0);
        std::fill_n(&b[8], 28, 0x20);
        b[0] = lun ? 0x7f : 0x00;
        b[2] = 0x05; b[3] = 0x01; b[4] = 52;
        static const char model[] = " SEAGATE          ST225N1.00";
        memcpy(&b[8], model, 28);
        static const uint8_t ext[] = { 0x00, 0x08, 0x00, 0x99, 0xa0, 0x27, 0x34, 0x01, 0x04, 0xa0,
                                       0x01, 0x18, 0x07, 0x00, 0xa0, 0x00, 0x00, 0xff };
        memcpy(&b[36], ext, sizeof(ext));
        if (size > 56) size = 56;
        tbuf.assign(b.begin(), b.begin() + size);
        t_data_in();
        break;
    }
    case 0x1a: {  // MODE SENSE(6)
        int lun = get_lun(cmd[1] >> 5);
        if (lun) { t_check(SK_ILLEGAL_REQUEST, 0x25); break; }
        int page = cmd[2] & 0x3f;
        int size = cmd[4];
        std::vector<uint8_t> b(1, 0);
        auto put = [&](uint8_t x) { b.push_back(x); };
        auto put16 = [&](uint32_t x) { put((uint8_t)(x >> 8)); put((uint8_t)x); };
        auto put24 = [&](uint32_t x) { put((uint8_t)(x >> 16)); put((uint8_t)(x >> 8)); put((uint8_t)x); };
        put(0x00); put(0x00);
        put(0x08); put(0x00); put24(block_count ? block_count - 1 : 0); put(0x00); put24(512);
        int pmax = page == 0x3f ? 0x3e : page, pmin = page == 0x3f ? 0 : page;
        bool fail = false;
        for (int p = pmax; p >= pmin; p--) {
            switch (p) {
            case 0x00: put(0x80); put(0x02); put(0); put(0); break;
            case 0x01: put(0x01); put(0x0a); put(0x26); for (int i = 0; i < 9; i++) put(0); break;
            case 0x02: put(0x02); put(0x0e); for (int i = 0; i < 14; i++) put(0); break;
            case 0x03:
                put(0x83); put(0x16); put16((uint32_t)cyl * heads);
                for (int i = 0; i < 6; i++) put(0);
                put16(sectors); put16(512);
                for (int i = 0; i < 10; i++) put(0);
                break;
            case 0x04:
                put(0x84); put(0x16); put24(cyl); put((uint8_t)heads);
                for (int i = 0; i < 15; i++) put(0);
                put16(10000); put(0); put(0);
                break;
            case 0x08: put(0x08); put(0x0a); for (int i = 0; i < 10; i++) put(0); break;
            case 0x30: {
                put(0xb0); put(0x16);
                static const char id[] = "APPLE COMPUTER, INC   ";
                for (int i = 0; i < 22; i++) put((uint8_t)id[i]);
                break;
            }
            default:
                if (page != 0x3f) fail = true;
                break;
            }
        }
        if (fail) { t_check(SK_ILLEGAL_REQUEST, 0x24); break; }
        b[0] = (uint8_t)(b.size() - 1);
        size_t n = std::min<size_t>(b.size(), (size_t)size);
        tbuf.assign(b.begin(), b.begin() + n);
        t_data_in();
        break;
    }
    case 0x1b:  // START STOP UNIT
    case 0x35:  // SYNCHRONIZE CACHE(10)
    case 0x04:  // FORMAT UNIT (no data written; see the header)
        t_status(SS_GOOD);
        break;
    case 0x15:  // MODE SELECT(6)
        if (cmd[4]) t_data_out(cmd[4]);
        else t_status(SS_GOOD);
        break;
    case 0x2f:  // VERIFY(10)
        if (!(cmd[1] & 0x02)) t_status(SS_GOOD);
        else t_check(SK_ILLEGAL_REQUEST, 0x20);
        break;
    case 0x25: {  // READ CAPACITY
        uint32_t last = block_count ? block_count - 1 : 0;
        uint8_t b[8] = { (uint8_t)(last >> 24), (uint8_t)(last >> 16), (uint8_t)(last >> 8), (uint8_t)last, 0, 0, 2, 0 };
        tbuf.assign(b, b + 8);
        t_data_in();
        break;
    }
    case 0x1c:    // RECEIVE DIAGNOSTIC RESULTS
    case 0x1d: {  // SEND DIAGNOSTIC (MAME returns data in for it too)
        if (op == 0x1d && (cmd[1] & 4)) { t_status(SS_GOOD); break; }
        int size = (int)u16(3);
        std::vector<uint8_t> b = { 0, 6, 0, 0, 0, 0, 0 };
        if (op == 0x1d) b.push_back(0);
        size_t n = std::min<size_t>(b.size(), (size_t)size);
        tbuf.assign(b.begin(), b.begin() + n);
        t_data_in();
        break;
    }
    default:     // nscsi_full_device::scsi_unknown_command
        t_check(SK_ILLEGAL_REQUEST, 0x00);
        break;
    }
}

struct a2scsi_data : public SlotData {
    MMU_II *megaii;
    A2Scsi *card;
    ResourceFile *rom_file;
};

uint8_t a2scsi_read_C0xx(void *context, uint32_t addr) {
    return ((a2scsi_data *)context)->card->c0n_r(addr & 0xf);
}
void a2scsi_write_C0xx(void *context, uint32_t addr, uint8_t value) {
    ((a2scsi_data *)context)->card->c0n_w(addr & 0xf, value);
}
uint8_t a2scsi_read_C8xx(void *context, uint32_t addr) {
    return ((a2scsi_data *)context)->card->c800_r((uint16_t)((addr & 0xffff) - 0xc800));
}
void a2scsi_write_C8xx(void *context, uint32_t addr, uint8_t value) {
    ((a2scsi_data *)context)->card->c800_w((uint16_t)((addr & 0xffff) - 0xc800), value);
}

void map_c8_a2scsi(void *context, SlotType_t slot) {
    a2scsi_data *d = (a2scsi_data *)context;
    for (uint8_t page = 0xC8; page <= 0xCF; page++) {
        d->megaii->map_c1cf_page_both(page, nullptr, "NONE");
        d->megaii->map_c1cf_page_read_h(page, { a2scsi_read_C8xx, d }, "A2SCSI");
        d->megaii->map_c1cf_page_write_h(page, { a2scsi_write_C8xx, d }, "A2SCSI");
    }
}

} // namespace

void init_slot_a2scsi(computer_t *computer, SlotType_t slot) {
    if (const char *lp = getenv("A2SCSI_LOG")) g_log = fopen(lp, "w");
    a2scsi_data *d = new a2scsi_data;
    d->id = DEVICE_ID_A2SCSI;
    d->_slot = slot;
    d->megaii = computer->mmu;
    d->card = new A2Scsi();

    d->rom_file = new ResourceFile("roms/cards/a2scsi/341-0437-a.bin", READ_ONLY);
    uint8_t *rd = d->rom_file->exists() ? d->rom_file->load() : nullptr;
    if (!rd || d->rom_file->size() < 0x4000) {
        std::cerr << "a2scsi: roms/cards/a2scsi/341-0437-a.bin (16,384 bytes) not found; the card's ROM reads $FF" << std::endl;
    } else {
        memcpy(d->card->rom, rd, 0x4000);
    }

    computer->mmu->set_slot_rom(slot, d->card->rom, "A2SCSI_ROM");

    storage_key_t key;
    key.slot = (uint16_t)slot;
    key.drive = 0;
    computer->mounts->register_storage_device(key, d->card, DRIVE_TYPE_PRODOS_BLOCK);

    for (int i = 0; i < 16; i++) {
        computer->mmu->set_C0XX_read_handler(0xC080 + (slot * 0x10) + i, { a2scsi_read_C0xx, d });
        computer->mmu->set_C0XX_write_handler(0xC080 + (slot * 0x10) + i, { a2scsi_write_C0xx, d });
    }
    computer->mmu->set_C8xx_handler(slot, map_c8_a2scsi, d);
}