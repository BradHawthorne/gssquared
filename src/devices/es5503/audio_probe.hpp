#pragma once
#include <cstdint>
#include <cstdlib>

/*
 * audio_probe -- the one place the generated sample stream can be observed.
 *
 * Every other audio behaviour in this tree is gated at its REGISTERS: periphcheck
 * round-trips DOC registers and sound RAM, spkcheck counts $C030 toggles and
 * proves their timestamps advance at 14 c14m per CPU cycle. None of that observes
 * a single SAMPLE. The chip could be clocked correctly, addressed correctly, and
 * emit silence -- or emit the same value forever -- and every existing check would
 * still pass.
 *
 * That gap is why the upstream stereo commits could not be ported honestly: a
 * change to per-channel routing is unverifiable when nothing can see a channel.
 *
 * This is a passive accumulator on the buffer that already exists, in the moment
 * between generate_samples() filling it and SDL consuming it. It adds one pass
 * over a batch and is compiled in unconditionally but costs nothing until armed,
 * because arming is what makes the counters move.
 *
 * Deliberately NOT a recording: no sample data is retained, only aggregates. The
 * question a gate needs answered is "is this stream alive, and is it the shape it
 * should be", not "what did it sound like".
 *
 * PER-CHANNEL, AND WHY IT HAS TO BE. The aggregate counters below cannot tell a
 * working stereo stream from one that dropped a channel: sum, min, max and
 * non-zero count all survive silencing one side. That is the same shape as the
 * silence hole this file was written to close -- a number that moves whether or
 * not the thing works. So every aggregate is also kept per channel, and the
 * channel count is reported rather than assumed, so a gate can state which
 * shape it is asserting against instead of inferring it from a sample total.
 */
namespace audio_probe {

// Interleaved index 0 is the first channel of a frame. Under the TN #19 stereo
// convention the emulator follows, that is LEFT and index 1 is RIGHT; the probe
// itself takes no position on which side is which -- it reports what arrived at
// each interleave slot, and the gate names the sides.
inline constexpr int MAX_CH = 2;

inline bool     g_on       = false;
inline int      g_channels = 0;   // channels in the most recent batch; 0 = none seen yet
inline uint64_t g_frames   = 0;   // frames (one sample per channel)
inline uint64_t g_samples  = 0;   // int16 samples across ALL channels
inline uint64_t g_nonzero  = 0;   // samples not exactly zero -- "is anything happening"
inline int32_t  g_min      = 0;   // smallest sample seen, any channel
inline int32_t  g_max      = 0;   // largest
inline uint64_t g_abs_sum  = 0;   // sum |sample| -- average level without keeping audio

inline uint64_t g_ch_nonzero[MAX_CH] = {0, 0};
inline int32_t  g_ch_min[MAX_CH]     = {0, 0};
inline int32_t  g_ch_max[MAX_CH]     = {0, 0};
inline uint64_t g_ch_abs_sum[MAX_CH] = {0, 0};

inline void reset() {
    g_channels = 0;
    g_frames = 0; g_samples = 0; g_nonzero = 0;
    g_min = 0; g_max = 0; g_abs_sum = 0;
    for (int c = 0; c < MAX_CH; c++) {
        g_ch_nonzero[c] = 0; g_ch_min[c] = 0; g_ch_max[c] = 0; g_ch_abs_sum[c] = 0;
    }
}

inline void arm(bool on) { g_on = on; if (on) reset(); }

// Called with each batch as it is handed to the audio stream. `frames` counts
// frames, not int16 samples: a 2-channel batch of 256 frames is 512 samples.
inline void note(const int16_t *buf, uint32_t frames, int channels) {
    if (!g_on || !buf || channels <= 0) return;
    g_channels = channels;
    for (uint32_t f = 0; f < frames; f++) {
        for (int c = 0; c < channels; c++) {
            const int32_t s = buf[(size_t)f * (size_t)channels + (size_t)c];
            const uint64_t mag = (uint64_t)(s < 0 ? -s : s);
            if (s) g_nonzero++;
            if (s < g_min) g_min = s;
            if (s > g_max) g_max = s;
            g_abs_sum += mag;
            // Channels beyond the pair still count toward the aggregates above;
            // only the per-channel breakdown is capped, and g_channels reports
            // the real width so a reader can see the breakdown is partial.
            if (c < MAX_CH) {
                if (s) g_ch_nonzero[c]++;
                if (s < g_ch_min[c]) g_ch_min[c] = s;
                if (s > g_ch_max[c]) g_ch_max[c] = s;
                g_ch_abs_sum[c] += mag;
            }
        }
    }
    g_frames  += frames;
    g_samples += (uint64_t)frames * (uint64_t)channels;
}

} // namespace audio_probe
