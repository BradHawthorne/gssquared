#pragma once
#include <cstdint>
#include <cstdlib>

/*
 * audio_probe -- the one place generated sample streams can be observed.
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
 * This is a passive accumulator on buffers that already exist, in the moment
 * between a device filling one and SDL consuming it. It adds one pass over a
 * batch and is compiled in unconditionally but costs nothing until armed,
 * because arming is what makes the counters move.
 *
 * Deliberately NOT a recording: no sample data is retained, only aggregates. The
 * question a gate needs answered is "is this stream alive, and is it the shape it
 * should be", not "what did it sound like".
 *
 * PER-CHANNEL, AND WHY IT HAS TO BE. The aggregate counters cannot tell a working
 * stereo stream from one that dropped a channel: sum, min, max and non-zero count
 * all survive silencing one side. That is the same shape as the silence hole this
 * file was written to close -- a number that moves whether or not the thing works.
 * So every aggregate is also kept per channel, and the channel count is reported
 * rather than assumed, so a gate can state which shape it is asserting against
 * instead of inferring it from a sample total.
 *
 * PER-SOURCE, FOR THE SAME REASON ONE LEVEL OUT. The Ensoniq and the drive sound
 * effects are different devices reaching SDL by different paths. A single set of
 * counters covering both would let "some audio happened" stand in for "the DRIVE
 * made a sound" -- and since the DOC is usually busy, that check would pass with
 * the drive silent. Sources are therefore separate accumulators and the rail
 * names which one it is reporting; nothing here ever sums across them.
 */
namespace audio_probe {

// Interleaved index 0 is the first channel of a frame. Under the TN #19 stereo
// convention the emulator follows, that is LEFT and index 1 is RIGHT; the probe
// itself takes no position on which side is which -- it reports what arrived at
// each interleave slot, and the gate names the sides.
inline constexpr int MAX_CH = 2;

enum src_id {
    SRC_DOC = 0,   // Ensoniq 5503, via soundglu's catch-up
    SRC_SFX = 1,   // drive sound effects, via SoundEffect
    N_SRC   = 2
};

struct source_t {
    int      channels = 0;   // channels in the most recent batch; 0 = none seen yet
    uint64_t frames   = 0;   // frames (one sample per channel)
    uint64_t samples  = 0;   // int16 samples across ALL channels
    uint64_t nonzero  = 0;   // samples not exactly zero -- "is anything happening"
    int32_t  min      = 0;   // smallest sample seen, any channel
    int32_t  max      = 0;   // largest
    uint64_t abs_sum  = 0;   // sum |sample| -- average level without keeping audio
    // Batches this source declined to read, because they were not in a format
    // it understands. Counted rather than ignored: a format the probe cannot
    // decode would otherwise be indistinguishable from silence, which is the
    // exact confusion this file exists to prevent.
    uint64_t unread   = 0;

    uint64_t ch_nonzero[MAX_CH] = {0, 0};
    int32_t  ch_min[MAX_CH]     = {0, 0};
    int32_t  ch_max[MAX_CH]     = {0, 0};
    uint64_t ch_abs_sum[MAX_CH] = {0, 0};
};

inline bool     g_on = false;
inline source_t g_src[N_SRC];

inline void reset() {
    for (int s = 0; s < N_SRC; s++) {
        g_src[s] = source_t();
    }
}

inline void arm(bool on) { g_on = on; if (on) reset(); }

// Called with each batch as it is handed to an audio stream. `frames` counts
// frames, not int16 samples: a 2-channel batch of 256 frames is 512 samples.
inline void note(int src, const int16_t *buf, uint32_t frames, int channels) {
    if (!g_on || src < 0 || src >= N_SRC) return;
    source_t &S = g_src[src];
    if (!buf || channels <= 0) { S.unread++; return; }
    S.channels = channels;
    for (uint32_t f = 0; f < frames; f++) {
        for (int c = 0; c < channels; c++) {
            const int32_t s = buf[(size_t)f * (size_t)channels + (size_t)c];
            const uint64_t mag = (uint64_t)(s < 0 ? -s : s);
            if (s) S.nonzero++;
            if (s < S.min) S.min = s;
            if (s > S.max) S.max = s;
            S.abs_sum += mag;
            // Channels beyond the pair still count toward the aggregates above;
            // only the per-channel breakdown is capped, and channels reports
            // the real width so a reader can see the breakdown is partial.
            if (c < MAX_CH) {
                if (s) S.ch_nonzero[c]++;
                if (s < S.ch_min[c]) S.ch_min[c] = s;
                if (s > S.ch_max[c]) S.ch_max[c] = s;
                S.ch_abs_sum[c] += mag;
            }
        }
    }
    S.frames  += frames;
    S.samples += (uint64_t)frames * (uint64_t)channels;
}

// A batch arrived that this source could not decode (unexpected sample format).
inline void note_unread(int src) {
    if (!g_on || src < 0 || src >= N_SRC) return;
    g_src[src].unread++;
}

} // namespace audio_probe
