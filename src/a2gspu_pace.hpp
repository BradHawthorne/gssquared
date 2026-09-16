#pragma once
// ============================================================================
// a2gspu_pace.hpp -- A2GSPU_PACE: whether a headless run waits for the wall clock.
//
// WHY: every frame ended in frame_sleep, a busy-wait to the next 16.69 ms
// deadline, in the windowed emulator and the headless runs alike. A headless run
// has no one watching. `run 600` on the CTRL rail took 10 s whatever it did, a
// 300-frame spike took 5 s, and each paced session held a CPU core spinning.
// For an agent driving the machine one command at a time, that wait was most of
// the rail's latency.
//
// Frames end at a cycle count, not a time, so skipping the wait changes nothing
// the CPU sees: the unarmed-hash gate proves the same hash, cycle count and run
// hash either way. What did read the wall clock is audio. The speaker generated
// a frame's samples only while SDL's queue was short, and SDL drains that queue
// in real time -- so sample generation, and every probe that counts it,
// depended on how fast the host played sound. Unpaced, SDL could not keep up
// and the DOC stream would grow without bound. So `fast` also pauses the audio
// device and empties the streams after each frame: every frame generates its
// samples (audio_probe sees all of them), nothing plays, nothing accumulates.
//
//   A2GSPU_PACE=fast       no frame wait; audio generated and probed, not played
//   A2GSPU_PACE=realtime   the stock wait (and audio output)
//   unset                  headless runs (spike, CTRL): fast; windowed: realtime
//
// Wall-clock devices still see less host time per emulated second when fast
// (an unpinned RTC or clock card); pin A2GSPU_FAKETIME when a run must repeat.
// PINS reports the pace so two runs can be compared knowing it.
// ============================================================================
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "a2gspu_rails_registry.hpp"   // a2rails::Registrar: this rail registers its own reader (M8)

inline bool g_a2gspu_pace_fast = false;
inline const char *g_a2gspu_pace_name = "realtime";

// Called once when a headless run starts, before PINS prints. Exits 2 on a
// value it does not know: a pace nobody asked for is not a default to guess.
inline void a2gspu_pace_arm_headless() {
    const char *e = getenv("A2GSPU_PACE");
    if (!e || !*e || !strcmp(e, "fast")) { g_a2gspu_pace_fast = true; g_a2gspu_pace_name = "fast"; }
    else if (!strcmp(e, "realtime"))      { g_a2gspu_pace_fast = false; g_a2gspu_pace_name = "realtime"; }
    else {
        fprintf(stderr, "A2GSPU_PACE: FAIL unknown pace '%s' -- want fast or realtime (exit 2)\n", e);
        exit(2);
    }
    printf("A2GSPU PACE: %s%s\n", g_a2gspu_pace_name,
           g_a2gspu_pace_fast ? " (frames do not wait for the wall clock; audio is generated and probed, not played)"
                              : " (frames wait for the wall clock, as windowed)");
}

// The readback (`rail read A2GSPU_PACE`).
inline void a2gspu_pace_format(char *buf, size_t n) {
    snprintf(buf, n, "status=OK pace=%s%s", g_a2gspu_pace_name,
             g_a2gspu_pace_fast ? " frame-wait=none audio=generated-not-played" : " frame-wait=wall-clock");
}

inline a2rails::Registrar g_reader_pace{"A2GSPU_PACE", a2gspu_pace_format};
