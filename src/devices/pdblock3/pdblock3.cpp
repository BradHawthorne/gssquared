/*
 *   Copyright (c) 2025-2026 Jawaid Bazyar

 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.

 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.

 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

//#include <algorithm>
#include <cstring>
#include <stdio.h>
//#include <stdio.h>
#include <filesystem>
#include <iostream>
#include <cstring>      // memset, for Format
#include <string>
#include <unordered_map>
#include <vector>
#include "gs2.hpp"
#include "cpu.hpp"
#include "mmus/mmu_ii.hpp"
#include "debug.hpp"
#include "devices/pdblock3/pdblock3.hpp"
#include "util/media.hpp"
#include "util/ResourceFile.hpp"
#include "util/mount.hpp"


class PDBlock3; // forward declaration

struct pdblock3_data: public SlotData {
    ResourceFile *rom;
    MMU *mmu;
    MMU_II *megaii;
    PDBlock3 *pdb;
};

class PDBlock3 : public StorageDevice {

    struct key_info_t {
        std::vector<std::string> tooltip;
        int last_active_unit;
    };

private:
    MMU *mmu;
    pdblock_cmd_buffer cmd_buffer;
    bool disk_switched[PDB3_MAX_UNITS];
    media_t drives[PDB3_MAX_UNITS];
    uint8_t _slot;

    std::unordered_map<storage_key_t, key_info_t> key_info;

    struct open_file_t {
        FILE *fp = nullptr;
        int refs = 0;
    };
    std::unordered_map<std::string, open_file_t> open_files;

    FILE *acquire_file(const std::string& filename, bool write_protected) {
        auto it = open_files.find(filename);
        if (it != open_files.end() && it->second.fp) {
            it->second.refs++;
            return it->second.fp;
        }
        const char *mode = write_protected ? "rb" : "r+b";
        FILE *fp = fopen(filename.c_str(), mode);
        if (fp == nullptr) return nullptr;
        open_files[filename] = {fp, 1};
        return fp;
    }

    void release_file(FILE *fp) {
        if (fp == nullptr) return;
        for (auto it = open_files.begin(); it != open_files.end(); ++it) {
            if (it->second.fp == fp) {
                it->second.refs--;
                if (it->second.refs <= 0) {
                    fclose(fp);
                    open_files.erase(it);
                }
                return;
            }
        }
        fclose(fp);
    }

public:
    // Emulated-clock-based disk timing (deterministic). Replaces host wall-clock
    // (SDL_GetTicksNS) so a headless boot trace is reproducible: the disk "motor on"
    // window GS/OS polls is now a fixed count of emulated cycles (~1s), not real time.
    NClock *clock = nullptr;
    static constexpr uint64_t PDB3_MOTOR_CYCLES = 1000000;

    PDBlock3(uint8_t slot, MMU *mmu) : _slot(slot), mmu(mmu) {
        for (int j = 0; j < PDB3_MAX_UNITS; j++) {
            drives[j].file = nullptr;
            drives[j].media = nullptr;
            disk_switched[j] = false;
        }
        // initialize key_info for all 6 devices
        for (int j = 0; j < 6; j++) {
            storage_key_t key;
            key.drive = j;
            key.slot = _slot;
            key_info[key] = {std::vector<std::string>(), -1};
            // TODO: key_info[key].last_active_unit = XXXX in read and write below.
        }
        cmd_buffer.index = 0;
        cmd_buffer.error = 0x00;
        cmd_buffer.status1 = 0x00;
        cmd_buffer.status2 = 0x00;
    }

    ~PDBlock3() {
        for (int j = 0; j < PDB3_MAX_UNITS; j++) {
            if (drives[j].file) {
                release_file(drives[j].file);
                drives[j].file = nullptr;
            }
        }
    }

    void read_from_memory(uint32_t addr, uint8_t *cb, uint8_t size) {
        for (int i = 0; i < size; i++) {
            cb[i] = mmu->read(addr + i);
        }
    }

    void write_to_memory(uint32_t addr, uint8_t *data, uint8_t size) {
        for (int i = 0; i < size; i++) {
            mmu->write(addr + i, data[i]);
        }
    }

    uint8_t internal_status(uint8_t drive) {
        if (drive >= PDB3_MAX_UNITS) return 0x01;
        if (drives[drive].file == nullptr) {
            return 0x01; // device not ready
        }
        return 0x00; // device ready
    }


    /**
    * These two routines read and write a block to the media.
    * They take into account the media descriptor and the data offset.
    * They read and write memory to the MMU using the MMU functions
    * that take into account the memory map, so, we can write data into
    * any bank selected as the CPU (or a 'DMA' device like us) would see it.
    * slot and drive here might be virtual as one physical slot can map drives
    * to a different virtual slot.
    */
    // Opt-in in-memory disk: if A2GSPU_RAMDISK is set in the environment, load the
    // entire image (header + data) into a RAM buffer once. read_block/write_block
    // then serve from RAM; writes never touch the host file (copy-on-write base).
    // Leaves drv.ram empty (== OFF) on any failure so the host-file path is kept.
    void maybe_load_ramdisk(media_t &drv) {
        if (getenv("A2GSPU_RAMDISK") == nullptr) return;
        media_descriptor *media = drv.media;
        if (media == nullptr || drv.file == nullptr) return;
        uint64_t want = media->data_offset + (uint64_t)media->block_count * media->block_size;
        if (want == 0 || want > (256ull * 1024 * 1024)) return; // sanity cap (256MB)
        std::vector<uint8_t> buf(want, 0);
        if (fseek(drv.file, 0, SEEK_SET) != 0) return;
        size_t got = fread(buf.data(), 1, (size_t)want, drv.file);
        // A short read is fine (image may be exactly data-sized); zero-fill the rest.
        if (got == 0) return;
        drv.ram = std::move(buf);
        drv.dirty.assign((media->block_count + 7) / 8, 0);
        if (DEBUG(DEBUG_PD_BLOCK))
            std::cout << "PDB3 RAMDISK: loaded " << got << " bytes for " << media->filestub << std::endl;
    }

    // drive is unit 0-based
    void read_block(uint8_t drive, uint32_t block, uint32_t addr) {
        uint8_t block_buffer[512];
        if (!check_valid_unit(drive)) return;
        if (!check_online(drive)) return;

        key_info[drives[drive].key].last_active_unit = drive; // mark this as the last active unit for this key

        FILE *fp = drives[drive].file;
        media_descriptor *media = drives[drive].media;

        if (media->block_size == 0 || media->block_size > 512) {
            cmd_buffer.error = PD_ERROR_IO;
            return;
        }
        if (block >= media->block_count) {
            cmd_buffer.error = PD_ERROR_IO;
            return;
        }
        uint64_t off = media->data_offset + (uint64_t)block * media->block_size;
        std::vector<uint8_t> &ram = drives[drive].ram;
        if (!ram.empty()) {
            // In-RAM path: no fseek/fread. Bounds already checked above.
            for (int i = 0; i < media->block_size; i++) {
                mmu->write(addr + i, (off + i < ram.size()) ? ram[off + i] : 0);
            }
        } else {
            if (fseek(fp, off, SEEK_SET) < 0) {
                cmd_buffer.error = PD_ERROR_IO;
            }
            size_t bytes_read = fread(block_buffer, 1, media->block_size, fp);
            if (bytes_read != media->block_size) {
                cmd_buffer.error = PD_ERROR_IO;
            }
            for (int i = 0; i < media->block_size; i++) {
                mmu->write(addr + i, block_buffer[i]);
            }
        }
        drives[drive].last_block_accessed = block;
        drives[drive].last_block_access_time = clock ? clock->get_cycles() : 0;
    }

    void write_block(uint8_t drive, uint32_t block, uint32_t addr) {

        uint8_t block_buffer[512];
        if (!check_valid_unit(drive)) return;
        if (!check_online(drive)) return;

        key_info[drives[drive].key].last_active_unit = drive; // mark this as the last active unit for this key

        FILE *fp = drives[drive].file;
        media_descriptor *media = drives[drive].media;

        if (media->block_size == 0 || media->block_size > 512) {
            cmd_buffer.error = PD_ERROR_IO;
            return;
        }

        if (block >= media->block_count) {
            cmd_buffer.error = PD_ERROR_IO;
            return;
        }
        if (media->write_protected) {
            cmd_buffer.error = PD_ERROR_WRITE_PROTECTED;
            return;
        }

        uint64_t off = media->data_offset + (uint64_t)block * media->block_size;
        std::vector<uint8_t> &ram = drives[drive].ram;
        if (!ram.empty()) {
            // In-RAM COW path: write stays in RAM, host file is never modified.
            if (!drives[drive].dirty.empty()) drives[drive].dirty[block >> 3] |= (uint8_t)(1 << (block & 7));
            for (int i = 0; i < media->block_size; i++) {
                if (off + i < ram.size()) ram[off + i] = mmu->read(addr + i);
            }
        } else {
            for (int i = 0; i < media->block_size; i++) {
                block_buffer[i] = mmu->read(addr + i);
            }
            fseek(fp, off, SEEK_SET);
            fwrite(block_buffer, 1, media->block_size, fp);
        }
        drives[drive].last_block_accessed = block;
        drives[drive].last_block_access_time = clock ? clock->get_cycles() : 0;
    }

    /* SmartPort Format ($03). A LOW-LEVEL format: it prepares the medium and
       leaves it blank. It does NOT write a filesystem -- the caller (GS/OS
       Finder "Erase Disk", ProDOS FILER) writes the directory itself afterwards,
       which is why formatting alone leaves a volume the OS still calls damaged.
       Zeroing every block is what that looks like on file-backed media.

       This was `assert(false)`. The build ships WITHOUT -DNDEBUG (CMakeLists
       overrides CMAKE_CXX_FLAGS_RELEASE to plain -O3), so that assert was live
       in the shipping binary: a guest formatting a SmartPort volume did not get
       a failure, it took the whole emulator down. Neither half of that is what
       the hardware does. */
    void format_device(uint8_t drive) {
        if (!check_valid_unit(drive)) return;
        if (!check_online(drive)) return;

        key_info[drives[drive].key].last_active_unit = drive;

        media_descriptor *media = drives[drive].media;
        if (media->block_size == 0 || media->block_size > 512) {
            cmd_buffer.error = PD_ERROR_IO;
            return;
        }
        if (media->write_protected) {
            cmd_buffer.error = PD_ERROR_WRITE_PROTECTED;
            return;
        }

        uint8_t zero[512];
        memset(zero, 0, sizeof(zero));

        std::vector<uint8_t> &ram = drives[drive].ram;
        if (!ram.empty()) {
            // In-RAM COW path: the format stays in RAM, exactly as writes do,
            // so a formatted --ram-disk never touches the host file.
            if (!drives[drive].dirty.empty()) std::fill(drives[drive].dirty.begin(), drives[drive].dirty.end(), (uint8_t)0xFF);
            uint64_t start = media->data_offset;
            uint64_t len   = (uint64_t)media->block_count * media->block_size;
            if (start < ram.size()) {
                if (start + len > ram.size()) len = ram.size() - start;
                memset(&ram[start], 0, (size_t)len);
            }
        } else {
            FILE *fp = drives[drive].file;
            for (uint32_t b = 0; b < media->block_count; b++) {
                fseek(fp, media->data_offset + (uint64_t)b * media->block_size, SEEK_SET);
                if (fwrite(zero, 1, media->block_size, fp) != media->block_size) {
                    cmd_buffer.error = PD_ERROR_IO;
                    return;
                }
            }
            fflush(fp);
        }
        cmd_buffer.error = PD_ERROR_NONE;
    }

    struct DriveInfo { uint8_t status; uint32_t blk_count; };

    DriveInfo get_drive_info(uint8_t unit_index) {
        uint32_t blkcount = drives[unit_index].media->block_count;
        if (blkcount == 0x1'0000) blkcount = 0xFFFF; // 32 MB quirk
        bool wp = drives[unit_index].media->write_protected;
        uint8_t st = wp ? 0b1011'0100 : 0b1111'0000;
        if (disk_switched[unit_index]) {
            st |= 0b0000'0001;
            disk_switched[unit_index] = false;
        }
        return {st, blkcount};
    }

    // 0-based unit (already had 1 subtracted)
    bool check_valid_unit(uint8_t unit) {
        if (unit >= PDB3_MAX_UNITS) {
            cmd_buffer.error = PD_ERROR_NO_DEVICE; // invalid unit
            return false;
        }
        return true;
    }
    bool check_unit_nonzero(uint8_t unit) {
        if (unit == 0) {
            cmd_buffer.error = PD_ERROR_NO_DEVICE; // invalid unit
            return false;
        }
        return true;
    }

    bool check_online(uint8_t unit) {
        if (drives[unit].file == nullptr || drives[unit].media == nullptr) {
            cmd_buffer.error = PD_ERROR_DEVICE_OFFLINE;
            return false;
        }
        return true;
    }

    // FAITHFULNESS FIX (SIG-023): number of devices to report from the SmartPort
    // driver STATUS(unit 0) call. Real SmartPort firmware returns the count of
    // ATTACHED devices on that controller (a IIgs boot volume = 1, occasionally
    // 2) — NOT the controller's maximum unit capacity. Reporting PDB3_MAX_UNITS
    // (32, an ARRAY-SIZE constant) made GS/OS's device dispatcher enumerate 32
    // phantom units per SmartPort slot (ident_slot, Device.Dispatcher.Src:1832)
    // × the slots that carry a pdblock3 card => hundreds of frames of dead
    // enumeration before the desktop. mount() packs drives contiguously from
    // unit 0, so a simple occupancy count == the real (contiguous) chain length.
    // A2GSPU_SP_NDEV overrides the count (diagnostic A/B; e.g. =32 reproduces
    // the pre-fix behavior). This makes the emulator MORE faithful to real HW.
    uint8_t sp_num_devices() {
        // Read once: this runs on every SmartPort STATUS call, and a getenv per
        // call is work the stock emulator should not do.
        static const char *ov = getenv("A2GSPU_SP_NDEV");
        if (ov) return (uint8_t)atoi(ov);
        uint8_t n = 0;
        for (int j = 0; j < PDB3_MAX_UNITS; j++) {
            if (drives[j].file != nullptr) n++;
        }
        return n;
    }

    // Single implementation for both standard and extended SmartPort commands.
    // CmdStatus / CmdRW / CmdControl select the right command-list struct;
    // Stat00 / Stat03 select the right response struct (3-byte vs 4-byte blk_count).
    template<typename CmdStatus, typename CmdRW, typename CmdControl,
             typename Stat00,    typename Stat03>
    void sp_execute_impl(uint8_t cmdnum, uint32_t clist_addr) {
        switch (cmdnum) {
            case 0x00: { // Status (pg 122)
                CmdStatus cmdlist;
                read_from_memory(clist_addr, (uint8_t *)&cmdlist, sizeof(cmdlist));
                uint32_t slptr = cmdlist.status_p;
                switch (cmdlist.status_code) {
                    case 0x00: { // device status (pg 122)
                        if (cmdlist.unit == 0) { // SmartPort Driver Status (pg 125)
                            sp_cmd0_statcode_00_driver s;
                            s.num_devices = sp_num_devices(); // SIG-023: real attached count, not PDB3_MAX_UNITS(32)
                            memset(s.reserved, 0x00, sizeof(s.reserved));
                            write_to_memory(slptr, (uint8_t *)&s, sizeof(s));
                            cmd_buffer.status1 = sizeof(s);
                        } else if (cmdlist.unit > PDB3_MAX_UNITS) {
                            cmd_buffer.error = 0x21; // invalid unit
                            break;
                        } else if (drives[cmdlist.unit-1].media == nullptr) { // drive offline
                            /* GS/OS wants the full response back, not just an error */
                            Stat00 s;
                            s.status    = 0b1110'0000; // device offline
                            s.blk_count = 0;
                            write_to_memory(slptr, (uint8_t *)&s, sizeof(s));
                            cmd_buffer.status1 = sizeof(s);
                        } else { // drive online
                            Stat00 s;
                            auto [st, blkcount] = get_drive_info(cmdlist.unit - 1);
                            s.status    = st;
                            s.blk_count = blkcount;
                            write_to_memory(slptr, (uint8_t *)&s, sizeof(s));
                            cmd_buffer.status1 = sizeof(s);
                        }
                        break;
                    }
                    case 0x01: cmd_buffer.error = 0x21; break; // return device control block
                    case 0x02: cmd_buffer.error = 0x21; break; // return newline status
                    case 0x03: { // device information block (DIB)
                        uint8_t effunit = cmdlist.unit - 1;
                        if (!check_valid_unit(effunit)) break;
                        if (!check_online(effunit)) break;

                        Stat03 s;
                        auto [st, blkcount] = get_drive_info(effunit);
                        s.status    = st;
                        s.blk_count = blkcount;
                        s.id_str_length = 9;
                        memcpy(s.id_str, "BAZFAST3        ", 16);
                        s.id_str[8]      = 'A' + effunit;
                        s.device_type    = 0x02;        // hard disk — GS/OS treats $05 as SCSI and demands a SCSI driver
                        s.device_subtype = 0b1100'0000; // extended smartport + disk-switch errors
                        s.version_1      = 0x03;        // version 3
                        s.version_0      = 0x00;        // .0
                        write_to_memory(slptr, (uint8_t *)&s, sizeof(s));
                        cmd_buffer.status1 = sizeof(s);
                        break;
                    }
                    default: cmd_buffer.error = 0x21; break; // BADCTL
                }
                break;
            }
            case 0x01: { // ReadBlock
                CmdRW cmdlist;
                read_from_memory(clist_addr, (uint8_t *)&cmdlist, sizeof(cmdlist));
                if (!check_unit_nonzero(cmdlist.unit)) break;
                read_block(cmdlist.unit - 1, cmdlist.block, cmdlist.addr);
                break;
            }
            case 0x02: { // WriteBlock
                CmdRW cmdlist;
                read_from_memory(clist_addr, (uint8_t *)&cmdlist, sizeof(cmdlist));
                if (!check_unit_nonzero(cmdlist.unit)) break;
                write_block(cmdlist.unit - 1, cmdlist.block, cmdlist.addr);
                break;
            }
            case 0x03: { // Format
                CmdRW cmdlist;
                read_from_memory(clist_addr, (uint8_t *)&cmdlist, sizeof(cmdlist));
                if (!check_unit_nonzero(cmdlist.unit)) break;
                format_device(cmdlist.unit - 1);
                break;
            }
            case 0x04: { // Control / Eject
                /* SmartPort TN #2: Before May 1988, control code $04 was device-specific.
                   It is now defined as EJECT. Devices without removable media return success. */
                CmdControl cmdlist;
                read_from_memory(clist_addr, (uint8_t *)&cmdlist, sizeof(cmdlist));
                if (!check_unit_nonzero(cmdlist.unit)) break;
                if (!check_valid_unit(cmdlist.unit)) break;
                if (cmdlist.code == 0x04) {
                    storage_key_t key;
                    key.drive = cmdlist.unit - 1;
                    key.slot  = _slot;
                    unmount(key);
                }
                break;
            }
            default:
                cmd_buffer.error = 0x21;
                break;
        }
    }

    void sp_execute(uint8_t version, pdblock_cmd_v2 *c, uint8_t cmdnum, uint32_t cb_addr) {
        cmd_buffer.error = cmd_buffer.status1 = cmd_buffer.status2 = 0x00;
        uint32_t clist_addr = mmu->read(cb_addr + 1) | (mmu->read(cb_addr + 2) << 8);
        sp_execute_impl<sp_cmd_status_st, sp_cmd_rw_st, sp_cmd_control_st,
                        sp_cmd0_statcode_00, sp_cmd0_statcode_03>(cmdnum, clist_addr);
    }

    void sp_execute_extended(uint8_t version, pdblock_cmd_v2 *c, uint8_t cmdnum, uint32_t cb_addr) {
        cmd_buffer.error = cmd_buffer.status1 = cmd_buffer.status2 = 0x00;
        uint32_t clist_addr = mmu->read(cb_addr + 1) | (mmu->read(cb_addr + 2) << 8) | (mmu->read(cb_addr + 3) << 16);
        sp_execute_impl<sp_cmd_status_ex, sp_cmd_rw_ex, sp_cmd_control_ex,
                        sp_cmd0_statcode_00_ex, sp_cmd0_statcode_03_ex>(cmdnum & 0x3F, clist_addr);
    }

    /* Version 1 command - ProDOS Block Device */
    void pd_execute() {
        uint8_t cmd, dev, unit, slot, drive;
        uint16_t block, addr;
    
        if (DEBUG(DEBUG_PD_BLOCK)) print_cmdbuffer();
    
        uint8_t cksum = 0;
        for (int i = 0; i < cmd_buffer.index; i++) {
            cksum ^= cmd_buffer.cmd[i];
        }
        if (cksum != 0x00) {
            if (DEBUG(DEBUG_PD_BLOCK)) std::cout << "pd_execute: Checksum error" << std::endl;
            cmd_buffer.error = 0x01;
            return;
        }
    
        // Version 1 command
        pdblock_cmd_v1 *cmdbuf = (pdblock_cmd_v1 *)cmd_buffer.cmd;
        cmd = cmdbuf->cmd;
        dev = cmdbuf->dev;
        block = cmdbuf->block;
        addr = cmdbuf->addr;
        slot = (dev >> 4) & 0b0111;
        drive = (dev >> 7) & 0b1;
    
        if (slot != (uint8_t)_slot) {
            cmd_buffer.error = PD_ERROR_IO;
            return;
        }
    
        if (DEBUG(DEBUG_PD_BLOCK)) std::cout << "pdblock2_execute: Unit: " << std::hex << (int)unit 
            << ", Block: " << std::hex << (int)block << ", Addr: " << std::hex << (int)addr << ", CMD: " 
            << std::hex << (int)cmd << std::endl;
    
        uint8_t st = internal_status(drive);
        if (st) {
            cmd_buffer.error = PD_ERROR_DEVICE_OFFLINE;
            return;
        }
        cmd_buffer.error = 0x00;
        cmd_buffer.status1 = 0x00;
        cmd_buffer.status2 = 0x00;
        if (cmd == 0x00) {
            media_descriptor *media = drives[drive].media;
            if (media == nullptr) {
                cmd_buffer.error = PD_ERROR_DEVICE_OFFLINE;
                return;
            }
            // TODO: check and handle disk switched? 
            cmd_buffer.status1 = media->block_count & 0xFF;
            cmd_buffer.status2 = (media->block_count >> 8) & 0xFF;
        } else if (cmd == 0x01) {
            read_block(drive, block, addr);
        } else if (cmd == 0x02) {
            write_block(drive, block, addr);
        } else if (cmd == 0x03) { // Format
            // The ProDOS block-device interface defines the same four commands
            // as SmartPort (0=STATUS 1=READ 2=WRITE 3=FORMAT), so this is the
            // same operation reached through the older entry point. It returned
            // NO_DEVICE, which told the guest the drive did not exist -- a
            // misleading error for a drive that had just answered a STATUS call.
            format_device(drive);
        }
    }
        
    /* Implementations of the StorageDevice interface */

    bool mounto(storage_key_t key, std::vector<media_descriptor *> media_list) {
        if (media_list.size() > 1) return false;
        media_descriptor *media = media_list[0];

        if (key.drive >= PDB3_MAX_UNITS) return false;
        //if (DEBUG(DEBUG_PD_BLOCK)) printf("Mounting ProDOS block device %s slot %d drive %d\n", media->filename, slot, drive);
        if (DEBUG(DEBUG_PD_BLOCK)) std::cout << "Mounting PDB3 device " << media->filename << " slot: " << _slot << " drive " << key.drive << std::endl;
        
        FILE *fp = acquire_file(media->filename, media->write_protected);
        if (fp == nullptr) {
            std::cerr << "Could not open PDB3 device file: " << media->filename << std::endl;
            return false;
        }
        drives[key.drive].file = fp;
        drives[key.drive].media = media;
        disk_switched[key.drive] = true;
        return true;
    }

    /* BazFast is a SmartPort block device, so it only speaks 512-byte blocks.
       140K is additionally rejected even when it is block-structured (a 140K
       .hdv is 280 blocks of 512): ProDOS assumes anything that size is a Disk II
       on a 5.25 controller, and misdrives it. Those belong in a 5.25 drive. */
    static const char *media_reject_reason(const media_descriptor *media) {
        if (media->media_type != MEDIA_BLK || media->block_size != 512) {
            return "not a 512-byte block image";
        }
        if (media->data_size == 140 * 1024) {
            return "140K images must be mounted on a 5.25\" drive";
        }
        return nullptr;
    }

    /* Same host path, or two paths that resolve to the same inode. */
    static bool same_host_file(const std::string& a, const std::string& b) {
        if (a.empty() || b.empty()) return false;
        if (a == b) return true;
        std::error_code ec;
        const std::filesystem::path pa(a);
        const std::filesystem::path pb(b);
        if (!std::filesystem::exists(pa, ec) || !std::filesystem::exists(pb, ec)) {
            return false;
        }
        return std::filesystem::equivalent(pa, pb, ec);
    }

    bool host_file_already_mounted(const std::string& filename) const {
        for (int j = 0; j < PDB3_MAX_UNITS; j++) {
            if (drives[j].media && same_host_file(drives[j].media->filename, filename)) {
                return true;
            }
        }
        return false;
    }

    bool mount(storage_key_t key, std::vector<media_descriptor *> media_list) {
        if (media_list.empty()) return false;
        if (key.drive >= PDB3_MAX_DEVICES) return false;

        // Vet every volume before opening any of them, so a bad entry in a
        // .pmap can't leave us with a half-populated set of units.
        // Sharing one host file across units is allowed only for APM partitions
        // in this same mount (same file, different data_offset). Remounting that
        // file onto another drive, or listing it twice at the same offset,
        // would give two writers the same bytes.
        for (size_t i = 0; i < media_list.size(); i++) {
            const media_descriptor *media = media_list[i];
            if (const char *why = media_reject_reason(media)) {
                std::cerr << "BazFast: refusing '" << media->filename << "': " << why << std::endl;
                return false;
            }
            if (host_file_already_mounted(media->filename)) {
                std::cerr << "BazFast: refusing '" << media->filename << "': already mounted" << std::endl;
                return false;
            }
            for (size_t k = 0; k < i; k++) {
                if (same_host_file(media_list[k]->filename, media->filename) &&
                    media_list[k]->data_offset == media->data_offset) {
                    std::cerr << "BazFast: refusing '" << media->filename << "': already mounted" << std::endl;
                    return false;
                }
            }
        }

        int unused_unit = 0;
        int first_mounted_unit = -1;
        std::vector<int> assigned;
        const size_t tooltip_before = key_info[key].tooltip.size();
        for (media_descriptor *media : media_list) {
            // find unused unit
            while (unused_unit < PDB3_MAX_UNITS) {
                if (drives[unused_unit].file == nullptr) break;
                unused_unit++;
            }
            if (unused_unit == PDB3_MAX_UNITS) {
                for (int u : assigned) {
                    release_file(drives[u].file);
                    drives[u].file = nullptr;
                    drives[u].media = nullptr;
                    drives[u].key = storage_key_t();
                }
                key_info[key].tooltip.resize(tooltip_before);
                return false; // no units free
            }

            key_info[key].tooltip.push_back(media->filestub.empty() ? media->filename : media->filestub);

            //if (DEBUG(DEBUG_PD_BLOCK)) printf("Mounting ProDOS block device %s slot %d drive %d\n", media->filename, slot, drive);
            if (DEBUG(DEBUG_PD_BLOCK)) std::cout << "Mounting PDB3 device " << media->filename << " slot: " << _slot << " drive " << key.drive << std::endl;
            
            FILE *fp = acquire_file(media->filename, media->write_protected);
            if (fp == nullptr) {
                std::cerr << "Could not open PDB3 device file: " << media->filename << std::endl;
                for (int u : assigned) {
                    release_file(drives[u].file);
                    drives[u].file = nullptr;
                    drives[u].media = nullptr;
                    drives[u].key = storage_key_t();
                }
                key_info[key].tooltip.resize(tooltip_before);
                return false;
            }
            drives[unused_unit].file = fp;
            drives[unused_unit].media = media;
            drives[unused_unit].key = key;  // mark this as being mounted on this key
            disk_switched[unused_unit] = true;
            assigned.push_back(unused_unit);

            // In-memory COW lever (opt-in via A2GSPU_RAMDISK): slurp the whole
            // image into RAM once at mount. Reads/writes then hit RAM, never the
            // host file -> zero per-block disk I/O and a non-persistent (COW) base.
            // Default-OFF: when the env is unset, ram stays empty and the original
            // fseek/fread/fwrite host-file path is used unchanged.
            maybe_load_ramdisk(drives[unused_unit]);

            if (first_mounted_unit == -1) first_mounted_unit = unused_unit;
        }
        if (key_info[key].last_active_unit == -1)
            key_info[key].last_active_unit = first_mounted_unit;

        return true;
    }

    bool unmounto(storage_key_t key) {
        if (key.drive >= PDB3_MAX_UNITS) return true;
        if (drives[key.drive].file) {
            release_file(drives[key.drive].file);
            drives[key.drive].file = nullptr;
            drives[key.drive].media = nullptr;
            disk_switched[key.drive] = true;
        }
        return true;
    }

    // unmount all units that have a matching key
    // clear the tooltip vector for this key
    bool unmount(storage_key_t key) {
        if (key.drive >= PDB3_MAX_DEVICES) return true;

        for (int i = 0; i < PDB3_MAX_UNITS; i++) {
            if (drives[i].key == key) {
                if (drives[i].file) {
                    release_file(drives[i].file);
                    drives[i].file = nullptr;
                }
                drives[i].media = nullptr;
                drives[i].key = storage_key_t();
                disk_switched[i] = true;
            }
        }
        key_info[key].tooltip.clear();
        key_info[key].last_active_unit = -1;

        /* if (drives[key.drive].file) {
            fclose(drives[key.drive].file);
            drives[key.drive].file = nullptr;
            drives[key.drive].media = nullptr;
            disk_switched[key.drive] = true;
        } */
        return true;
    }

    bool writeback(storage_key_t key) {
        return true;
    }

    drive_status_t statuso(storage_key_t key) {
        if (key.drive >= PDB3_MAX_UNITS || drives[key.drive].media == nullptr) return {false, "", false, 0, false, false};
        
        media_t seldrive = drives[key.drive];

        bool motor = false;

        uint64_t curtime = clock ? clock->get_cycles() : 0;
        if (curtime - seldrive.last_block_access_time < PDB3_MOTOR_CYCLES) {
            motor = true;
        }
        // 3.5 drives turn off immediately.
        std::string fname;
        bool mounted = false;
        if (seldrive.media) {
            fname = seldrive.media->filestub;
            mounted = true;
        }
        
        return {mounted, fname, motor, seldrive.last_block_accessed, false,
                seldrive.media->write_protected};
    }

    drive_status_t status(storage_key_t key) {
        if (key.drive >= PDB3_MAX_UNITS) {  
            return {false, "", false, 0, false, false};
        }

        int last_active_unit = key_info[key].last_active_unit;
        if (last_active_unit == -1) {
            // TODO: should return status on the first unit that is mounted on this key.
            return {false, "", false, 0, false, false};
        }
        media_t seldrive = drives[last_active_unit];

        if (seldrive.media == nullptr) return {false, "", false, 0, false, false};

        bool motor = false;

        uint64_t curtime = clock ? clock->get_cycles() : 0;
        if (curtime - seldrive.last_block_access_time < PDB3_MOTOR_CYCLES) {
            motor = true;
        }
        // 3.5 drives turn off immediately.
        std::string fname;
        bool mounted = false;
        if (seldrive.media) {
            fname = seldrive.media->filestub;
            mounted = true;
        }
        
        return {mounted, fname, motor, seldrive.last_block_accessed, false,
                seldrive.media->write_protected};
    }


    void print_cmdbuffer() {
        std::cout << "PD3_CMD_BUFFER: ";
        for (int i = 0; i < cmd_buffer.index; i++) {
            std::cout << std::hex << (int)cmd_buffer.cmd[i] << " ";
        }
        std::cout << std::endl;
    }
    
    void reset() {
        cmd_buffer.index = 0;
    }
    void put(uint8_t data) {
        if (cmd_buffer.index < MAX_PD_BUFFER_SIZE) {
            cmd_buffer.cmd[cmd_buffer.index] = data;
            cmd_buffer.index++;
        }
    }

    /* Execute command - inspect command buffer version and dispatch to appropriate handler */

    void execute() {
        uint8_t version = cmd_buffer.cmd[0];
        if (version == 0x01) {
            pd_execute();
        } else if (version == 0x02) {
            pdblock_cmd_v2 *c = (pdblock_cmd_v2 *)cmd_buffer.cmd;
            uint32_t cb_addr = c->cmd_blk;
            uint8_t cmdnum = mmu->read(cb_addr);
            if (cmdnum & 0x40) sp_execute_extended(version, c, cmdnum, cb_addr);
            else sp_execute(version, c, cmdnum, cb_addr);
        } else {
            // TODO: return some kind of error
            if (DEBUG(DEBUG_PD_BLOCK)) std::cout << "pdblock3_execute: Version not supported" << std::endl;
            cmd_buffer.error = 0x01;
        }
        reset();
    }

    /* Getters for command status */
    uint8_t get_error() {
        return cmd_buffer.error;
    }
    uint8_t get_status1() {
        return cmd_buffer.status1;
    }
    uint8_t get_status2() {
        return cmd_buffer.status2;
    }


    // ---- A2GSPU snapshot (M5): the command buffer and each unit's state, and
    // the volume OVERLAY of a unit served from its RAM copy: every block
    // written since the image was loaded. The image is not in the file; a
    // unit's block names its image and block count and is applied only when
    // the same image is mounted now, and only in RAM mode -- without it the
    // host file is the volume, and it is never written from a snapshot.
public:
    void A2GSPU_snapshot(FILE *f) {
        fwrite(&cmd_buffer.index, 1, 1, f); fwrite(cmd_buffer.cmd, 1, MAX_PD_BUFFER_SIZE, f);
        fwrite(&cmd_buffer.error, 1, 1, f); fwrite(&cmd_buffer.status1, 1, 1, f); fwrite(&cmd_buffer.status2, 1, 1, f);
        uint32_t units = PDB3_MAX_UNITS; fwrite(&units, 4, 1, f);
        for (uint32_t u = 0; u < PDB3_MAX_UNITS; u++) {
            media_t &d = drives[u];
            const bool present = d.file && d.media;
            uint8_t hdr[3] = { (uint8_t)present, (uint8_t)disk_switched[u], (uint8_t)!d.ram.empty() };
            fwrite(hdr, 1, 3, f);
            if (!present) continue;
            const std::string name = d.media->filename;
            uint32_t n = (uint32_t)name.size(); fwrite(&n, 4, 1, f); if (n) fwrite(name.data(), 1, n, f);
            uint32_t bc = d.media->block_count; uint16_t bs = (uint16_t)d.media->block_size;
            fwrite(&bc, 4, 1, f); fwrite(&bs, 2, 1, f);
            int32_t lba = (int32_t)d.last_block_accessed; fwrite(&lba, 4, 1, f);
            uint64_t lat = d.last_block_access_time; fwrite(&lat, 8, 1, f);
            uint32_t ndirty = 0;
            if (!d.ram.empty() && !d.dirty.empty())
                for (uint32_t b = 0; b < bc; b++) if (d.dirty[b >> 3] & (1 << (b & 7))) ndirty++;
            fwrite(&ndirty, 4, 1, f);
            if (!d.ram.empty())
                printf("A2GSPU SNAP: pdblock unit %u %s: %u of %u blocks written since load, carried as the overlay\n",
                       u, name.c_str(), ndirty, bc);
            if (ndirty)
                for (uint32_t b = 0; b < bc; b++) if (d.dirty[b >> 3] & (1 << (b & 7))) {
                    const uint64_t off = d.media->data_offset + (uint64_t)b * bs;
                    fwrite(&b, 4, 1, f);
                    if (off + bs <= d.ram.size()) fwrite(&d.ram[off], 1, bs, f);
                    else { uint8_t z[512] = {0}; fwrite(z, 1, bs, f); }
                }
        }
    }
    // `mismatch` counts units whose overlay was not applied: a different image,
    // a different size, or a unit not in RAM mode. The block is always read whole.
    bool A2GSPU_restore(FILE *f, int *mismatch) {
        if (fread(&cmd_buffer.index, 1, 1, f) != 1 || fread(cmd_buffer.cmd, 1, MAX_PD_BUFFER_SIZE, f) != MAX_PD_BUFFER_SIZE) return false;
        if (fread(&cmd_buffer.error, 1, 1, f) != 1 || fread(&cmd_buffer.status1, 1, 1, f) != 1 || fread(&cmd_buffer.status2, 1, 1, f) != 1) return false;
        uint32_t units = 0; if (fread(&units, 4, 1, f) != 1 || units != PDB3_MAX_UNITS) return false;
        for (uint32_t u = 0; u < units; u++) {
            uint8_t hdr[3]; if (fread(hdr, 1, 3, f) != 3) return false;
            if (!hdr[0]) continue;
            uint32_t n = 0; if (fread(&n, 4, 1, f) != 1 || n > 4096) return false;
            std::string name(n, '\0'); if (n && fread(&name[0], 1, n, f) != n) return false;
            uint32_t bc = 0; uint16_t bs = 0; int32_t lba = 0; uint64_t lat = 0; uint32_t ndirty = 0;
            if (fread(&bc, 4, 1, f) != 1 || fread(&bs, 2, 1, f) != 1 || fread(&lba, 4, 1, f) != 1 ||
                fread(&lat, 8, 1, f) != 1 || fread(&ndirty, 4, 1, f) != 1) return false;
            if (bs == 0 || bs > 512 || ndirty > bc) return false;
            media_t &d = drives[u];
            const bool same = d.file && d.media && d.media->filename == name && d.media->block_count == bc &&
                              d.media->block_size == bs;
            const bool apply = same && !d.ram.empty();
            if (!apply) { if (mismatch) (*mismatch)++; }
            else {
                // The RAM copy may hold this session's writes: back to the image
                // (the host file is never modified in RAM mode), then the overlay.
                if (fseek(d.file, 0, SEEK_SET) == 0) {
                    size_t got = fread(d.ram.data(), 1, d.ram.size(), d.file);
                    if (got < d.ram.size()) memset(d.ram.data() + got, 0, d.ram.size() - got);
                }
                std::fill(d.dirty.begin(), d.dirty.end(), (uint8_t)0);
                disk_switched[u] = hdr[1] != 0;
                d.last_block_accessed = lba;
                d.last_block_access_time = lat ? (clock ? clock->get_cycles() : 0) : 0;   // the motor window is relative to the clock
            }
            for (uint32_t k = 0; k < ndirty; k++) {
                uint32_t b = 0; uint8_t data[512];
                if (fread(&b, 4, 1, f) != 1 || fread(data, 1, bs, f) != bs) return false;
                if (!apply || b >= bc) continue;
                const uint64_t off = d.media->data_offset + (uint64_t)b * bs;
                if (off + bs <= d.ram.size()) { memcpy(&d.ram[off], data, bs); d.dirty[b >> 3] |= (uint8_t)(1 << (b & 7)); }
            }
        }
        return true;
    }
};

// The one instance, for the snapshot code (set by init_pdblock3).
static PDBlock3 *g_a2gspu_pdblock3 = nullptr;


void pdblock3_write_C0x0(void *context, uint32_t addr, uint8_t data) {
    pdblock3_data * pdblock_d = (pdblock3_data *)context;
    uint16_t adr = addr & 0xF;

    if (adr == 0x00) {
        if (DEBUG(DEBUG_PD_BLOCK)) std::cout << "PD3_CMD_RESET: " << std::hex << (int)data << std::endl;
        pdblock_d->pdb->reset();
    } else if (adr == 0x01) {
        if (DEBUG(DEBUG_PD_BLOCK)) std::cout << "PD3_CMD_PUT: " << std::hex << (int)data << std::endl;
        pdblock_d->pdb->put(data);
    } else if (adr == 0x02) {
        if (DEBUG(DEBUG_PD_BLOCK)) std::cout << "PD3_CMD_EXECUTE: " << std::hex << (int)data << std::endl;
        pdblock_d->pdb->execute();
    } 
}

uint8_t pdblock3_read_C0x0(void *context, uint32_t addr) {
    pdblock3_data * pdblock_d = (pdblock3_data *)context;
    uint8_t val;

    if ((addr & 0xF) == 0x03) {
        val = pdblock_d->pdb->get_error();
        if (DEBUG(DEBUG_PD_BLOCK)) std::cout << "PD3_ERROR_GET: " << std::hex << (int)val << std::endl;
        return val;
    } else if ((addr & 0xF) == 0x04) {
        val = pdblock_d->pdb->get_status1();
        if (DEBUG(DEBUG_PD_BLOCK)) std::cout << "PD3_STATUS1_GET: " << std::hex << (int)val << std::endl;
        return val;
    } else if ((addr & 0xF) == 0x05) {
        val = pdblock_d->pdb->get_status2();
        if (DEBUG(DEBUG_PD_BLOCK)) std::cout << "PD3_STATUS2_GET: " << std::hex << (int)val << std::endl;
        return val;
    } else return 0xE0;
}


void map_rom_pdblock3(void *context, SlotType_t slot) {
    pdblock3_data * pdblock_d = (pdblock3_data *)context;

    uint8_t *dp = pdblock_d->rom->get_data();
    for (uint8_t page = 0; page < 8; page++) {
        pdblock_d->megaii->map_c1cf_page_read_only(page + 0xC8, dp + (page * 0x100), "PDB3_ROM");
    }
    if (DEBUG(DEBUG_PD_BLOCK)) {
        printf("mapped in PDB3 $C800-$CFFF\n");
    }
}

void init_pdblock3(computer_t *computer, SlotType_t slot)
{
    if (DEBUG(DEBUG_PD_BLOCK)) std::cout << "Initializing PDB3 slot " << slot << std::endl;
    pdblock3_data * pdblock_d = new pdblock3_data;
    pdblock_d->id = DEVICE_ID_PD_BLOCK2;
    
    pdblock_d->mmu = computer->cpu->mmu;
    pdblock_d->megaii = computer->mmu; // these could be the same (iie) or different (iigs)
    pdblock_d->_slot = slot;

    pdblock_d->rom = new ResourceFile("roms/cards/pdblock3/pdblock3.rom", READ_ONLY);
    if (pdblock_d->rom == nullptr) {
        std::cerr << "Failed to load pdblock3.rom" << std::endl;
        return;
    }
    pdblock_d->rom->load();

    // memory-map the page. Refactor to have a method to get and set memory map.
    uint8_t *rom_data = (uint8_t *)(pdblock_d->rom->get_data());

    // register slot ROM
    computer->mmu->set_slot_rom(slot, rom_data, "PDBLK_ROM");

    // register drives with mounts for status reporting
    PDBlock3 *pd3 = new PDBlock3(slot, pdblock_d->mmu);
    pdblock_d->pdb = pd3;
    g_a2gspu_pdblock3 = pd3;   // the snapshot code reaches the block device through this
    pd3->clock = computer->clock; // deterministic disk timing (see PDB3_MOTOR_CYCLES)

    storage_key_t key;
    key.slot = (uint16_t)slot;
    key.drive = 0;
    key.partition = 0;
    key.subunit = 0;
    for (key.drive = 0; key.drive < 6; key.drive++) {
        computer->mounts->register_storage_device(key, pd3, DRIVE_TYPE_PRODOS_BLOCK);
    }

    // register.. uh, registers.
    computer->mmu->set_C0XX_write_handler((slot * 0x10) + PD_CMD_RESET, { pdblock3_write_C0x0, pdblock_d });
    computer->mmu->set_C0XX_write_handler((slot * 0x10) + PD_CMD_PUT, { pdblock3_write_C0x0, pdblock_d });
    computer->mmu->set_C0XX_write_handler((slot * 0x10) + PD_CMD_EXECUTE, { pdblock3_write_C0x0, pdblock_d });
    computer->mmu->set_C0XX_read_handler((slot * 0x10) + PD_ERROR_GET, { pdblock3_read_C0x0, pdblock_d });
    computer->mmu->set_C0XX_read_handler((slot * 0x10) + PD_STATUS1_GET, { pdblock3_read_C0x0, pdblock_d });
    computer->mmu->set_C0XX_read_handler((slot * 0x10) + PD_STATUS2_GET, { pdblock3_read_C0x0, pdblock_d });
    
    computer->mmu->set_C8xx_handler(slot, map_rom_pdblock3, pdblock_d);

}

// ---- A2GSPU snapshot (M5) ---------------------------------------------------
void pdblock3_A2GSPU_snapshot(FILE *f) { if (g_a2gspu_pdblock3) g_a2gspu_pdblock3->A2GSPU_snapshot(f); }
bool pdblock3_A2GSPU_restore(FILE *f, int *mismatch) { return g_a2gspu_pdblock3 ? g_a2gspu_pdblock3->A2GSPU_restore(f, mismatch) : false; }
bool pdblock3_A2GSPU_present() { return g_a2gspu_pdblock3 != nullptr; }
