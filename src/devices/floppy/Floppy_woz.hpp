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

#include <string>
#include <cstdio>
#include <cstdint>
#include <cstdio>

#include "util/woz.hpp"
#include "util/SoundEffect.hpp"
#include "NClock.hpp"
#include "util/media.hpp"
#include "util/media_identity.hpp"
#include "util/mount.hpp"


class EventTimer;
class Woz_Nibblizer;

// Abstract base for floppy drives backed by a WOZ in-memory bit stream.
//
// The WOZ bit-stream model, the angular head position, read/write-pulse
// plumbing, and the mount/unmount/writeback/status path are all identical
// for 5.25 and 3.5 drives. Subclasses differ in:
//
//   - how set_phase()/set_hdsel() affects the drive (5.25 stepper detent
//     vs. 3.5 CA0-2/SEL/LSTRB 16-way status/control selector);
//   - how CPU cycles convert to head advance (head_advance_per_cycle(),
//     4 cycles per bit cell on 5.25 vs. 2 cycles per bit cell on 3.5);
//   - which WOZ TMAP slot corresponds to the current head position
//     (current_tmap_index(), quarter-track vs. track*2+side).
class Floppy_woz /* : public FloppyDrive */ {

protected:

    static constexpr uint64_t POSITION_FP_SHIFT = 3+4;
    static constexpr uint64_t POSITION_FP_MUL = 8*16;
    static constexpr uint64_t POSITION_FP_MASK = POSITION_FP_MUL - 1;
    static constexpr uint64_t POSITION_ADV_PER_CYCLE = 16;

    struct Position {
        uint64_t pos;
        uint64_t fract;

    };

    SoundEffect *sound_effect;
    NClockII *clock;

    Woz woz;
    // Bit stream for the currently-selected track; nullptr = empty track.
    // Non-const because write_pulse() mutates bits in place.
    woz_track_t *cur_track_ptr = nullptr;

    // Physical disk position in 1/8-bit-cell units.
    //   bit index into track = position >> 3  (mod track_bits)
    //   advance per CPU cycle = head_advance_per_cycle()
    uint64_t last_cycle = 0;
    uint64_t read_position = 0;  // where the LSS has consumed bits to
    uint64_t head_position = 0;  // true simulated angular head position
    uint64_t advance_per_cycle = 0;

    bool     enable        = false;  // IWM drive selected (ENABLE'). On 5.25 this
                                     // doubles as motor-on; on 3.5 the spindle is
                                     // separate — see lss_disk_spinning().
    uint8_t  write_protect = 0;

    bool is_mounted = false;
    bool modified   = false;
    media_descriptor *media_d = nullptr;

    uint64_t random_bits = 0x5FCB9E767DC3523A;
    uint32_t windowBits  = 0;

    EventTimer *event_timer = nullptr;
    FILE       *dbglog      = nullptr;

    virtual uint64_t get_current_time() { return clock->get_cycles(); }

    // Angular-preserving track switch. Looks up the new track via
    // current_tmap_index() and rescales the angular head position so disk
    // rotation continues naturally when the new track has a different
    // bit_count (critical for 3.5 variable-length zones; a no-op when
    // lengths match, as they do on 5.25).
    void update_track_ptr();

    // RNG used when four zero bits in a row trip the "fake bit" shim,
    // mirroring real WOZ-unclean track behaviour.
    uint8_t get_random_bit();

    // Hook: how many 1/8-bit-cell units head_position advances per CPU cycle.
    //   5.25: 2  (4 cycles per bit cell @ 4 us)
    //   3.5 : 4  (2 cycles per bit cell @ 2 us)
    virtual uint64_t head_advance_per_cycle() const = 0;

    // Hook: which WOZ TMAP slot (0..159) identifies the track currently
    // under the head. 5.25 returns the quarter-track index; 3.5 returns
    // (track_num << 1) | side.
    virtual int current_tmap_index() const = 0;

    // True while the LSS should advance the bit stream (fast_forward /
    // read_pulse / write_pulse). 5.25: same as enable. 3.5: enable plus
    // spindle command and media present.
    virtual bool lss_disk_spinning() const { return enable; }

    void note_spinning_inputs_changed(bool was_spinning) {
        if (!was_spinning && lss_disk_spinning()) {
            last_cycle = get_current_time();
        }
    }

public:
    Floppy_woz(SoundEffect *sound_effect, NClockII *clock, EventTimer *event_timer)
        : sound_effect(sound_effect), clock(clock), event_timer(event_timer) {}
    virtual ~Floppy_woz() = default;

    virtual Woz_Nibblizer* make_nibblizer(media_descriptor *media) { return nullptr; };

    // ── FloppyDrive contract: shared across 5.25 and 3.5 ─────────────────
    // Implemented in Floppy_woz.cpp; subclasses override only when behavior
    // differs (e.g. Floppy35_woz mount/status/unmount).
    virtual bool mount(uint64_t key, media_descriptor *media);
    virtual bool unmount(uint64_t key);
    virtual bool writeback();
    virtual drive_status_t status();
    virtual void reset();

    virtual uint8_t read_pulse();
    virtual void    write_pulse(uint8_t bit);
    // FORK: whether /WRREQ reaches this drive's surface right now (write_pulse asks).
    virtual bool    write_gate() const { return true; }

    virtual void set_phase(uint8_t phase, uint8_t onoff) = 0;

    virtual int get_track() = 0;

    virtual bool get_enable() { return enable; }
    virtual void set_enable(bool on) {
        const bool was_spinning = lss_disk_spinning();
        enable = on;
        note_spinning_inputs_changed(was_spinning);
    }

    virtual bool is_bitstream_spinning() const { return lss_disk_spinning(); }

    // Advance the head based on elapsed cycles since the previous call;
    // returns how many whole bit cells the LSS should clock through.
    virtual uint64_t fast_forward(/* uint64_t now */);

    inline uint8_t get_write_protect() { return write_protect; }

    // 3.5 SEL line (DISKREG bit 7). Base is a no-op so 5.25 can ignore it.
    virtual void set_hdsel(bool on) { (void)on; }

    // Legacy nybble API: unused in the WOZ/LSS path but required by the
    // FloppyDrive interface.
    //virtual void    write_nybble(uint8_t nybble) override { (void)nybble; }
    //virtual uint8_t read_nybble() override { return 0; }

    virtual uint8_t read_sense() = 0;

    // Legacy command-reg shims: intentionally asserts — Floppy_woz drives
    // are always driven through the LSS path in IWM/diskii_controller.
    //uint8_t read_cmd(uint16_t address) override;
    //void    write_cmd(uint16_t address, uint8_t data) override;

    void set_dbglog(FILE *f) { dbglog = f; }

    // Accessors for debugging
    virtual bool get_motor_on() { return enable; }

    virtual int get_side() = 0;

    virtual void debug(DebugFormatter *f) = 0;

    // FORK: A2GSPU_DISK_TIMING=1 -- the IWM's write cells come from its own clock, which
    // starts with the write state, so the first cell boundary is a whole cell after entry.
    // Drops the head's sub-cell phase; the bit under the head does not change.
    void restart_cell_phase() { head_position &= ~POSITION_FP_MASK; }

    void tick_no_write() {
        read_position += POSITION_FP_MUL;
        if (cur_track_ptr && cur_track_ptr->bit_count > 0) {
            read_position %= cur_track_ptr->bit_count * POSITION_FP_MUL;
        }
    }

    virtual void play_sound(uint64_t sound_effect_id) {
        sound_effect->play(sound_effect_id);
    }

    virtual Position get_pos_head() {
        return {head_position >> POSITION_FP_SHIFT, head_position & POSITION_FP_MASK};
    }

    virtual Position get_pos_read() {
        return {read_position >> POSITION_FP_SHIFT, read_position & POSITION_FP_MASK};
    }

    // ---- A2GSPU snapshot (M5): the head, not the media. The block names the
    // mounted image (its name and content hash, util/media_identity.hpp); a restore
    // applies it only when the same image is mounted now, and counts a mismatch for
    // the caller to warn about. A subclass writes its own fields after these and
    // applies them under the same rule.
public:
    bool a2gspu_is_mounted() const { return is_mounted; }   // an observer's question (A2GSPU_TAP_SESSION disk_writes)
    virtual void A2GSPU_snapshot(FILE *f) {
        media_identity_write(f, (is_mounted && media_d) ? media_d : nullptr);
        uint64_t u[4] = { read_position, head_position, advance_per_cycle, random_bits }; fwrite(u, 8, 4, f);
        fwrite(&windowBits, 4, 1, f);
        uint8_t b[4] = { (uint8_t)enable, write_protect, (uint8_t)is_mounted, (uint8_t)modified }; fwrite(b, 1, 4, f);
    }
    virtual bool A2GSPU_restore(FILE *f, int *media_mismatch) {
        media_identity_t id; if (!media_identity_read(f, id)) return false;
        uint64_t u[4]; if (fread(u, 8, 4, f) != 4) return false;
        uint32_t wb = 0; if (fread(&wb, 4, 1, f) != 1) return false;
        uint8_t b[4]; if (fread(b, 1, 4, f) != 4) return false;
        a2gspu_restore_apply = media_identity_matches(id, (is_mounted && media_d) ? media_d : nullptr);
        if (!a2gspu_restore_apply) { if (media_mismatch) (*media_mismatch)++; return true; }
        read_position = u[0]; head_position = u[1]; advance_per_cycle = u[2]; random_bits = u[3]; windowBits = wb;
        enable = b[0] != 0; write_protect = b[1]; modified = b[3] != 0;   // is_mounted is this machine's fact, not the file's
        last_cycle = get_current_time();   // the bit clock restarts now; the position is what it was
        return true;
    }
    // ---- A2GSPU snapshot (M5): the media overlay. The head block above is the
    // drive; this is what is on the disk. A disk written since it was mounted
    // saves every track's bits (the WOZ image as the drive now holds it); an
    // unwritten one saves only its name. A restore applies the tracks only to the
    // same image with the same track map and count -- anything else is counted
    // for the caller to warn about -- so a restored machine reads what the saved
    // one wrote. The host file is never written from a snapshot.
public:
    void A2GSPU_media_snapshot(FILE *f) {
        media_identity_write(f, (is_mounted && media_d) ? media_d : nullptr);
        const uint8_t has = (is_mounted && modified) ? 1 : 0; fwrite(&has, 1, 1, f);
        if (!has) return;
        const woz_image_t &img = woz.image();
        fwrite(img.tmap, 1, 160, f);
        uint32_t nt = (uint32_t)img.tracks.size(); fwrite(&nt, 4, 1, f);
        for (const woz_track_t &t : img.tracks) {
            uint32_t hdr[2] = { t.bit_count, (uint32_t)t.bits.size() }; fwrite(hdr, 4, 2, f);
            if (hdr[1]) fwrite(t.bits.data(), 1, hdr[1], f);
        }
    }
    bool A2GSPU_media_restore(FILE *f, int *mismatch, int *applied) {
        media_identity_t id; if (!media_identity_read(f, id)) return false;
        uint8_t has = 0; if (fread(&has, 1, 1, f) != 1) return false;
        if (!has) return true;
        uint8_t tmap[160]; if (fread(tmap, 1, 160, f) != 160) return false;
        uint32_t nt = 0; if (fread(&nt, 4, 1, f) != 1 || nt > 256) return false;
        std::vector<woz_track_t> saved(nt);
        for (woz_track_t &t : saved) {
            uint32_t hdr[2]; if (fread(hdr, 4, 2, f) != 2 || hdr[1] > (1u << 20)) return false;
            t.bit_count = hdr[0]; t.bits.resize(hdr[1]);
            if (hdr[1] && fread(t.bits.data(), 1, hdr[1], f) != hdr[1]) return false;
        }
        woz_image_t &img = woz.image();
        if (!media_identity_matches(id, (is_mounted && media_d) ? media_d : nullptr) || nt != img.tracks.size() || memcmp(tmap, img.tmap, 160) != 0) {
            if (mismatch) (*mismatch)++;
            return true;
        }
        const uint64_t head = head_position;
        for (uint32_t i = 0; i < nt; i++) { img.tracks[i].bits = std::move(saved[i].bits); img.tracks[i].bit_count = saved[i].bit_count; }
        modified = true;
        cur_track_ptr = nullptr;                 // the track under the head was replaced: take it fresh,
        update_track_ptr();                      // then keep the head block's angle
        head_position = head; read_position = head;
        if (cur_track_ptr && cur_track_ptr->bit_count) { head_position %= cur_track_ptr->bit_count * POSITION_FP_MUL; read_position = head_position; }
        if (applied) (*applied)++;
        return true;
    }
protected:
    bool a2gspu_restore_apply = true;   // the base decided from the media name; the subclass follows it
};
