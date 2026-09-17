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

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <iostream>

#include "Floppy_woz.hpp"
//#include "devices/diskii/diskii_fmt.hpp"
#include "util/SoundEffectKeys.hpp"
#include "util/woz_nibblizer.hpp"

// ───────────────────────────── mount/unmount/writeback ─────────────────────────

bool Floppy_woz::mount(uint64_t key, media_descriptor *media_in) {
    if (is_mounted) {
        unmount(key);
    }

    int rc = 0;
    if (media_in->media_type == MEDIA_WOZ) {
        rc = woz.load(media_in->filename);
    } else {
        auto nibblizer = make_nibblizer(media_in);
        if (!nibblizer) return false;
        rc = nibblizer->import_block_image(woz, media_in);
        delete nibblizer;   // FORK: make_nibblizer returns a new object; it was never freed
    }
 //                : woz.import_from_media(media_in);

    if (rc != 0) {
        fprintf(stderr, "Floppy_woz: failed to load/import '%s'\n",
                media_in->filename.c_str());
        is_mounted = false;
        return false;
    }

    read_position = 0;
    head_position = 0;
    last_cycle    = get_current_time();
    // Force update_track_ptr() to treat this as a fresh track (old_bits == 0
    // skips the angular-rescale path so we start at position 0).
    cur_track_ptr = nullptr;

    update_track_ptr();

    write_protect = media_in->write_protected;
    // FORK: a WOZ image's INFO chunk can say the disk is write protected [WOZ 2 reference,
    // INFO +2]; the drive's sense follows it, as it follows a locked file or a 2MG lock.
    if (media_in->media_type == MEDIA_WOZ && woz.image().info.write_protected) {
        write_protect = 1;
        media_in->write_protected = true;
    }
    is_mounted    = true;
    media_d       = media_in;
    modified      = false;
    media_identity_capture(media_in);   // FORK: the image as mounted, for snapshots

    play_sound(SE_SHUGART_CLOSE);

    advance_per_cycle = head_advance_per_cycle();

    std::cout << "Floppy_woz: mounted " << media_in->filestub << std::endl;
    return true;
}

bool Floppy_woz::unmount(uint64_t key) {
    //(void)key;
    // Reset WOZ image to a clean blank state.
    woz = Woz{};
    cur_track_ptr = nullptr;

    read_position = 0;
    head_position = 0;
    last_cycle    = 0;
    // Recomputed lazily by fast_forward() against the now-blank Woz (whose
    // optimal_bit_timing is the standard default), so an empty spinning
    // drive keeps clocking noise bits at a sane rate.
    advance_per_cycle = 0;

    is_mounted = false;
    media_d    = nullptr;
    modified   = false;

    play_sound(SE_SHUGART_OPEN);
    return true;
}

// FORK: the disk as written, beside an original whose format cannot hold it.
static bool save_written_sidecar(Woz &woz, const media_descriptor *media, const char *why) {
    const std::string sidecar = media->filename + ".written.woz";
    if (woz.save(sidecar) != 0) {
        fprintf(stderr, "Floppy_woz: '%s': %s, and the sidecar '%s' could not be written either; "
                        "the guest's writes to this disk are LOST (the original is untouched)\n",
                media->filename.c_str(), why, sidecar.c_str());
        return false;
    }
    fprintf(stderr, "Floppy_woz: '%s': %s. The original was NOT touched; the disk as written is saved "
                    "as '%s'\n", media->filename.c_str(), why, sidecar.c_str());
    return true;
}

bool Floppy_woz::writeback() {
    if (!media_d) return false;

    // FORK: nothing is written unless the guest wrote. Every mount through the CTRL
    // rail and every close through the dialog is a SAVE_AND_UNMOUNT, so a disk that
    // was only ever read used to be re-encoded back over its own file on every swap.
    if (!modified) {
        std::cout << "Floppy_woz: " << media_d->filestub << " unchanged; not written" << std::endl;
        return true;
    }
    // FORK: a write-protected image (a read-only file, a locked 2MG, a WOZ whose INFO says
    // so) is never written back, whatever reached its surface in memory.
    if (media_d->write_protected) {
        fprintf(stderr, "Floppy_woz: '%s' is write protected; what reached its surface is not saved "
                        "(the original is untouched)\n", media_d->filename.c_str());
        modified = false;
        return true;
    }

    // MEDIA_NYBBLE means the mount source was a 143K block image
    // (.do/.po/.dsk): decode the in-memory WOZ bit stream back into a raw
    // disk_image_t and rewrite the file in place. For native WOZ images
    // just save as WOZ2.
    // TODO: we should dealloc the nibblizer here on exit
    std::cout << "Floppy_woz: writing back disk image " << media_d->filename
              << " (media_type=" << media_d->media_type << ")" << std::endl;

    if ((media_d->media_type == MEDIA_NYBBLE) || (media_d->media_type == MEDIA_BLK)) {
        auto nibblizer = make_nibblizer(media_d);
        if (!nibblizer) return false;
        std::cout << "Floppy_woz: writing back block disk image" << std::endl;
        int rc = nibblizer->export_block_image(woz, media_d);
        delete nibblizer;
        if (rc == Woz_Nibblizer::EXPORT_LOSSY) {
            if (!save_written_sidecar(woz, media_d, "not every sector decodes, so a block image cannot hold it")) {
                return false;
            }
        } else if (rc != 0) {
            fprintf(stderr, "Floppy_woz: block export failed for '%s' (the original is untouched)\n",
                    media_d->filename.c_str());
            return false;
        }
#if 0
        disk_image_t out{};
        if (woz.export_to_disk_image(out, media_d->interleave) != 0) {
            fprintf(stderr,
                    "Floppy_woz: WOZ->block export had decode errors for '%s'; "
                    "writing partial result\n",
                    media_d->filename.c_str());
            // Fall through and write whatever was recovered, matching
            // Floppy525::writeback()'s always-write behaviour.
        }
        if (!write_disk_image_po_do(media_d, out)) {
            fprintf(stderr, "Floppy_woz: block writeback failed for '%s'\n",
                    media_d->filename.c_str());
            return false;
        }
#endif
    } else if (media_d->media_type == MEDIA_WOZ && !woz.image().unsaved_chunks.empty()) {
        // FORK: save() writes INFO, TMAP, TRKS and META only. Over this file it would drop
        // the rest (a WOZ 2.1 FLUX chunk and the tracks it names, WRIT), so the disk as
        // written goes to the sidecar.
        std::string names;
        for (const auto &c : woz.image().unsaved_chunks) names += (names.empty() ? "" : ",") + c;
        const std::string why = "the image carries chunks this build does not write (" + names + ")";
        if (!save_written_sidecar(woz, media_d, why.c_str())) {
            return false;
        }
    } else if (media_d->media_type == MEDIA_WOZ) {
        std::cout << "Floppy_woz: writing back WOZ disk image" << std::endl;
        int rc = woz.save(media_d->filename);
        if (rc != 0) {
            fprintf(stderr, "Floppy_woz: writeback failed for '%s'\n",
                    media_d->filename.c_str());
            return false;
        }
    } else {
        // FORK: a .nib (or any other source that is not WOZ or a block image) used to
        // fall through to woz.save(filename) -- overwriting the user's .nib with WOZ
        // bytes under the .nib name.
        if (!save_written_sidecar(woz, media_d, "its format cannot hold what the guest wrote")) {
            return false;
        }
    }

    modified = false;
    return true;
}

// ───────────────────────────── status / reset ─────────────────────────────────

drive_status_t Floppy_woz::status() {
    if (is_mounted) {
        return {is_mounted, media_d->filestub, enable, get_track(), modified,
                media_d->write_protected};
    }
    return {is_mounted, "", enable, get_track(), modified, false};
}

void Floppy_woz::reset() {
    enable = false;
}

// ───────────────────────────── track / bit stream helpers ────────────────────

void Floppy_woz::update_track_ptr() {
    // FORK: an empty quarter-track counts as empty_track_bits() long, so stepping across
    // one keeps the disk's angle [WOZ 2 reference: 51,200 bits on a 5.25 disk].
    const uint64_t old_bits = track_len();
    cur_track_ptr = woz.get_track_ptr(current_tmap_index());
    const uint64_t new_bits = track_len();

    // Preserve angular position across a track change. 5.25 tracks are all
    // near-equal length so the old/new ratio is ~1 and this degenerates to
    // a plain modulus; 3.5 tracks vary by zone and the scale is essential.
    if (new_bits && old_bits) {
        // 128-bit intermediate to avoid overflow on the multiply.
        head_position = static_cast<uint64_t>(
            ((__uint128_t)head_position * new_bits) / old_bits);
    }
    if (new_bits) {
        head_position %= new_bits * POSITION_FP_MUL;
    }
    read_position = head_position;
}

uint64_t Floppy_woz::fast_forward(/* uint64_t now */) {
    uint64_t now = get_current_time(); // use our own clock.
    uint64_t elapsed = now - last_cycle;
    last_cycle = now;

    if (!lss_disk_spinning()) {
        return 0;  // disk is not spinning
    }

    // Bound LSS catch-up work. While the motor is on but no IWM register
    // accesses are happening, head_position keeps advancing but read_position
    // is frozen (only read_pulse/write_pulse move it). When the CPU finally
    // pokes the IWM, the lag can be tens of thousands of bits, and we'd
    // burn that many LSS iterations on output nobody will ever see. The
    // LSS only has a few bits of effective state (data_register=8,
    // internal_data_register=8, sequencer_state=1, windowBits' 4-zero
    // detector=4, async_shift_reg=8); once we've fed it more than ~16
    // bits, all prior history is gone. So when the lag exceeds this cap
    // we snap read_position forward to leave a warm-up window in front
    // of the head and discard the rest. The IWM2 fast_forward_impl loop
    // drives one bit per iteration, so after MAX_BITS_TO_SIM iterations
    // read_position lands exactly on (head_position >> 3).
    constexpr uint64_t MAX_BITS_TO_SIM = 32;

    // advance_per_cycle is normally (re)computed at mount time, but a drive
    // can spin with NO media mounted — a real spindle turns regardless, and
    // the read chain sees noise (the no-track branch below synthesizes
    // random bits). Left at its initial 0, an empty drive's head would never
    // advance, the LSS would never clock, and firmware that runs the IWM
    // write-handshake protocol against an empty drive (the IIgs ROM03 POST
    // disk self-test does exactly this) polls a handshake bit that can never
    // update. A fresh/blank Woz's optimal_bit_timing defaults to the
    // standard 5.25 value (32), so the subclass hook is safe to call while
    // unmounted.
    if (advance_per_cycle == 0) {
        advance_per_cycle = head_advance_per_cycle();
    }

    // Always advance head_position so angular position stays consistent,
    // including across no-track periods. update_track_ptr() rescales /
    // modulos head_position when a track later becomes available.
    head_position += (elapsed * advance_per_cycle);

    if (track_len() == 0) {
        // No track under the head: read_pulse() will synthesize random
        // bits for the LSS. read_position doesn't wrap in this state
        // (read_pulse skips the modulo when bit_count==0), so we just
        // measure raw lag and apply the same cap.
        // FORK: only where an empty track's length is unknown (3.5); a 5.25 empty track
        // wraps like any other (empty_track_bits).
        uint64_t bits_to_sim = (head_position >> POSITION_FP_SHIFT) - (read_position >> POSITION_FP_SHIFT);
        if (bits_to_sim > MAX_BITS_TO_SIM) {
            read_position = ((head_position >> POSITION_FP_SHIFT) - MAX_BITS_TO_SIM) * POSITION_FP_MUL;
            bits_to_sim = MAX_BITS_TO_SIM;
        }
        return bits_to_sim;
    }

    uint64_t track_bits = track_len();

    // Derive the number of whole bit cells to simulate from the fractional
    // positions already accumulated. Using (head_fp >> 3) - (read_fp >> 3)
    // is critical: head_position may be mid-cell, and naive elapsed/4 would
    // permanently skip bits on a straddling boundary. The modulo collapses
    // multi-rotation lag (LSS state can't tell the difference).
    uint64_t bits_to_sim =
        ((head_position >> POSITION_FP_SHIFT) - (read_position >> POSITION_FP_SHIFT)) % track_bits;

    if (bits_to_sim > MAX_BITS_TO_SIM && !writing) {
        uint64_t head_bit = (head_position >> POSITION_FP_SHIFT) % track_bits;
        uint64_t snap_bit =
            (head_bit + track_bits - MAX_BITS_TO_SIM) % track_bits;
        read_position = snap_bit * POSITION_FP_MUL;
        bits_to_sim = MAX_BITS_TO_SIM;
    }
    return bits_to_sim;
}

uint8_t Floppy_woz::get_random_bit() {
    random_bits = (random_bits << 1) | (random_bits >> 63);
    return random_bits & 1;
}

uint8_t Floppy_woz::read_pulse() {
    uint8_t bit;
    if (!lss_disk_spinning() || !cur_track_ptr || cur_track_ptr->bit_count == 0) {
        bit = 0;  // Randomized below via the 4-zero-bit window check.
    } else {
        uint64_t track_bits  = cur_track_ptr->bit_count;
        uint64_t bi          = (read_position >> POSITION_FP_SHIFT) % track_bits;
        uint64_t byte_idx    = bi >> 3;
        uint64_t bit_in_byte = bi & 7;
        size_t   need_bytes  = (static_cast<size_t>(track_bits) + 7) / 8;
        //uint16_t track_size = cur_track_ptr->bits.size();
        /* if (byte_idx < cur_track_ptr->bits.size() &&
            cur_track_ptr->bits.size() >= need_bytes) { */
        if (bi < cur_track_ptr->bit_count) {
            bit = (cur_track_ptr->bits[byte_idx] >> (7 - static_cast<int>(bit_in_byte))) & 1;
        } else {
            bit = 0;  // WOZ metadata vs. buffer inconsistency: synthesize.
        }
    }

    bit = read_amplifier(bit);
    read_position += POSITION_FP_MUL;
    if (const uint64_t len = track_len()) {
        read_position %= len * POSITION_FP_MUL;
    }
    return bit;
}

void Floppy_woz::write_pulse(uint8_t bit) {
    if (lss_disk_spinning() && cur_track_ptr && cur_track_ptr->bit_count > 0 && write_gate()) {
        uint64_t track_bits  = cur_track_ptr->bit_count;
        uint64_t bi          = (read_position >> POSITION_FP_SHIFT) % track_bits;
        uint64_t byte_idx    = bi >> 3;
        uint64_t bit_in_byte = bi & 7;
        size_t   need_bytes  = (static_cast<size_t>(track_bits) + 7) / 8;
        /* if (byte_idx < cur_track_ptr->bits.size() &&
            cur_track_ptr->bits.size() >= need_bytes) { */
        if (bi < cur_track_ptr->bit_count) { // WOZ metadata vs. buffer inconsistency, disregard.
            uint8_t mask = static_cast<uint8_t>(1u << (7 - static_cast<int>(bit_in_byte)));
            if (bit & 1) cur_track_ptr->bits[byte_idx] |= mask;
            else         cur_track_ptr->bits[byte_idx] &= static_cast<uint8_t>(~mask);
            modified = true;
        }
    }
    read_position += POSITION_FP_MUL;
    if (const uint64_t len = track_len()) {
        read_position %= len * POSITION_FP_MUL;
    }
}

// ───────────────────────────── unused cmd shims ──────────────────────────────

/* uint8_t Floppy_woz::read_cmd(uint16_t address) {
    (void)address;
    assert(false && "Floppy_woz::read_cmd is not used in the IWM/LSS path");
    return 0;
}

void Floppy_woz::write_cmd(uint16_t address, uint8_t data) {
    (void)address;
    (void)data;
    assert(false && "Floppy_woz::write_cmd is not used in the IWM/LSS path");
} */
