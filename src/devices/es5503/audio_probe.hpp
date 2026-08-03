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
 */
namespace audio_probe {

inline bool     g_on      = false;
inline uint64_t g_samples = 0;   // total samples seen
inline uint64_t g_nonzero = 0;   // samples not exactly zero -- "is anything happening"
inline int32_t  g_min     = 0;   // smallest sample seen
inline int32_t  g_max     = 0;   // largest
inline uint64_t g_abs_sum = 0;   // sum |sample| -- average level without keeping audio

inline void reset() {
    g_samples = 0; g_nonzero = 0; g_min = 0; g_max = 0; g_abs_sum = 0;
}

inline void arm(bool on) { g_on = on; if (on) reset(); }

// Called with each batch as it is handed to the audio stream.
inline void note(const int16_t *buf, uint32_t n) {
    if (!g_on || !buf) return;
    for (uint32_t i = 0; i < n; i++) {
        const int32_t s = buf[i];
        if (s) g_nonzero++;
        if (s < g_min) g_min = s;
        if (s > g_max) g_max = s;
        g_abs_sum += (uint64_t)(s < 0 ? -s : s);
    }
    g_samples += n;
}

} // namespace audio_probe
