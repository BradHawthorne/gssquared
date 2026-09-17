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

#pragma once

#include <cstdint>
#include <cstdlib>

#include "Floppy_woz.hpp"
#include "util/DebugFormatter.hpp"

// 5.25 Shugart-style drive: the four phase lines form a stepper-motor
// detent that directly positions the head in quarter-track increments.
class Floppy525_woz : public Floppy_woz {
    uint8_t rw_mode = 0;

    uint8_t phase0 = 0;
    uint8_t phase1 = 0;
    uint8_t phase2 = 0;
    uint8_t phase3 = 0;

    uint16_t image_index = 0;
    int16_t  track       = 0;  // quarter-track index (clamped by max_tracks at mount)
    // Inclusive max quarter-track index from max(140, WOZ TMAP span); default 139.
    int16_t  max_tracks  = 139;

    // Unique EventTimer key for phase-settle events. Must be unique across all
    // Floppy525_woz instances sharing a timer (multi Disk II + IWM drive pairs).
    // Layout: 0xABAB0000 | (slot << 8) | drive  — distinct from Floppy35's 0xABAC*.
    uint64_t instanceID = 0;

    // 0 = Drive 1 (left channel), 1 = Drive 2 (right channel). The eject and
    // insert sounds come from the DRIVE, not the controller, so the drive has
    // to know its own side.
    uint16_t drive_index = 0;

    void update_track();
    static void phase_change_callback(uint64_t instanceID, void *userData);

protected:
    uint64_t head_advance_per_cycle() const override { 
        //return 2;
        // 32 here is standard bit timing for 5.25" drives.
        return (2 * POSITION_ADV_PER_CYCLE * 32) / woz.image().info.optimal_bit_timing;
    }

    int      current_tmap_index()     const override { return track; }

    // FORK: PH1 and PH3 on together is the SmartPort bus-enable state, which no Disk II
    // stepper sequence uses, and on the IIgs disk port 5.25 drives share the chain with
    // SmartPort devices. A drive that wrote then would take every SmartPort packet onto
    // its disk: the IIgs ROM's boot probe on the 5.25 port did, 19 bytes onto track 0 of
    // any mounted disk, saved into a WOZ on unmount. Real IIgs boots do not damage 5.25
    // disks, so the write does not reach the surface. [Whether the drive or the IIgs glue
    // gates it is unverified; the drive's write-protect sense reading 1 with PH1 on, above,
    // is what makes the probe send.]
    bool write_gate() const override { return !(phase1 && phase3); }

    // FORK: A2GSPU_DISK_TIMING=1 -- real time. The IWM clocks its bit cells from FCLK, not
    // the CPU, so a 4 us cell stays 4 us at 2.8 MHz; counting CPU cycles made it 1.4 us.
    // Video cycles are the machine's 1.0205 MHz timebase (Floppy35_woz uses them too).
    bool real_time = false;
public:
    uint64_t get_current_time() override { return real_time ? clock->get_vid_cycles() : clock->get_cycles(); }
protected:

public:
    Floppy525_woz(SoundEffect *sound_effect, NClockII *clock, EventTimer *event_timer,
                  uint16_t slot, uint16_t drive)
        : Floppy_woz(sound_effect, clock, event_timer), drive_index(drive) {
            instanceID = 0xABAB0000ull | (static_cast<uint64_t>(slot) << 8) | drive;
            const char *dt = getenv("A2GSPU_DISK_TIMING");
            real_time = dt && dt[0] == '1';
        }

    void play_sound(uint64_t sound_effect_id) override {
        const SoundChannel ch =
            (drive_index == 0) ? SoundChannel::Left : SoundChannel::Right;
        sound_effect->play(sound_effect_id, ch);
    }

    bool mount(uint64_t key, media_descriptor *media) override;
    bool unmount(uint64_t key) override;

    virtual Woz_Nibblizer* make_nibblizer(media_descriptor *media) override;

    virtual void set_phase(uint8_t phase, uint8_t onoff) override;

    virtual int get_track() override { return track; }

    int16_t get_max_tracks() const { return max_tracks; }

    uint8_t get_phase(uint8_t phase) const {
        switch (phase) {
            case 0: return phase0;
            case 1: return phase1;
            case 2: return phase2;
            case 3: return phase3;
            default: return 0;
        }
    }

    bool get_is_mounted() const { return is_mounted; }
   
    virtual int get_side() override { return 0; }

    virtual uint8_t read_sense() override;

    void debug(DebugFormatter *f) override {
        f->addLine("Image: %s", woz.get_current_filename().c_str());
        f->addLine("Optimal Bit Timing: %d (advance_per_cycle: %llu)", woz.image().info.optimal_bit_timing, advance_per_cycle);
        f->addLine("enable: %d ph [%d,%d,%d,%d]", enable, phase0, phase1, phase2, phase3);
        f->addLine("Track: %d.%d [ max: %d.%d ]", track/4, track%4, max_tracks/4, max_tracks%4);
        f->addLine("Track Bits: %llu", (unsigned long long)(cur_track_ptr ? cur_track_ptr->bit_count : 0));

        Position pos_head = get_pos_head();
        Position pos_read = get_pos_read();
        f->addLine("Head Position: %llu.%llu", pos_head.pos, pos_head.fract);
        f->addLine("Read Position: %llu.%llu", pos_read.pos, pos_read.fract);
        f->addLine("Last Cycle: %llu", (unsigned long long)last_cycle);
        f->addLine("Modified: %d", modified);
    }

    // ---- A2GSPU snapshot (M5): the stepper phases and the quarter-track ----
public:
    void A2GSPU_snapshot(FILE *f) override {
        Floppy_woz::A2GSPU_snapshot(f);
        uint8_t b[5] = { rw_mode, phase0, phase1, phase2, phase3 }; fwrite(b, 1, 5, f);
        int16_t t[2] = { track, max_tracks }; fwrite(t, 2, 2, f);
        fwrite(&image_index, 2, 1, f);
    }
    bool A2GSPU_restore(FILE *f, int *media_mismatch) override {
        if (!Floppy_woz::A2GSPU_restore(f, media_mismatch)) return false;
        uint8_t b[5]; if (fread(b, 1, 5, f) != 5) return false;
        int16_t t[2]; if (fread(t, 2, 2, f) != 2) return false;
        uint16_t ii = 0; if (fread(&ii, 2, 1, f) != 1) return false;
        if (!a2gspu_restore_apply) return true;
        rw_mode = b[0]; phase0 = b[1]; phase1 = b[2]; phase2 = b[3]; phase3 = b[4];
        track = t[0]; max_tracks = t[1]; image_index = ii;
        update_track_ptr();   // the bit stream follows the head
        return true;
    }
};
