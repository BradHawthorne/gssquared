#pragma once
/*
 * a2gspu_faketime.hpp -- A2GSPU_FAKETIME for every host-time read a guest can see.
 *
 * The IIgs RTC honoured the pin (devices/rtc/RTC.hpp) and nothing else did: the
 * ProDOS clock card and the ThunderClock Plus read the host's local time, and the
 * VGC one-second interrupt took its phase from the host clock, so a pinned run with
 * either card, or with the one-second interrupt enabled, did not repeat.
 *
 * Same meaning as the RTC's pin: the decimal unix seconds are the wall-clock instant
 * the machine's clocks show, with NO host timezone applied (the RTC adds only the
 * 1904 epoch delta), so every clock in a pinned machine agrees with the others.
 * It is a HARNESS knob: software under development must not depend on it.
 */
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>

// The pinned instant, or false when A2GSPU_FAKETIME is unset or empty. Under
// A2GSPU_FAKETIME_TICK this is the ORIGIN: the instant at emulated time zero.
inline bool a2gspu_faketime_unix(long long *out) {
    const char *e = std::getenv("A2GSPU_FAKETIME");
    if (!e || !*e) return false;
    *out = std::strtoll(e, nullptr, 10);
    return true;
}

/*
 * A TICKING PIN (fidelity study, sound and time section 7). A frozen clock is one a
 * real machine never shows for long: GS/OS's clock display stands still, elapsed-time
 * code sees none, and a timeout that waits for the seconds to change never ends.
 * A2GSPU_FAKETIME_TICK=1 keeps the pinned instant as the origin and advances it one
 * second per 14,318,180 ticks of EMULATED 14M time -- never host time -- so a pinned
 * run still repeats exactly. The seconds step on the same edges as the VGC one-second
 * interrupt (display.cpp aligns its first edge to them). Frozen stays the default
 * while consumers' pinned hashes depend on it; the default is the owner's decision.
 *
 * The emulated clock is installed by computer_t (the time source is per machine, and
 * this header stays free of the clock's).
 */
inline uint64_t (*g_a2gspu_timesource_c14m)(void *ctx) = nullptr;
inline void *g_a2gspu_timesource_ctx = nullptr;
constexpr uint64_t A2GSPU_C14M_PER_SECOND = 14'318'180;   // every NClock mode's c14M_per_second

inline void a2gspu_timesource_install(uint64_t (*c14m)(void *), void *ctx) {
    g_a2gspu_timesource_c14m = c14m;
    g_a2gspu_timesource_ctx = ctx;
}

inline bool a2gspu_faketime_ticking() {
    const char *e = std::getenv("A2GSPU_FAKETIME_TICK");
    return e && *e && std::strcmp(e, "0") != 0;
}

// Whole emulated seconds since power-on (0 with no clock installed).
inline long long a2gspu_emulated_seconds() {
    if (!g_a2gspu_timesource_c14m) return 0;
    return (long long)(g_a2gspu_timesource_c14m(g_a2gspu_timesource_ctx) / A2GSPU_C14M_PER_SECOND);
}

// The instant the pinned clocks show NOW: the pin itself when frozen, the origin plus
// emulated seconds when ticking. False when unpinned.
inline bool a2gspu_pinned_now_unix(long long *out) {
    if (!a2gspu_faketime_unix(out)) return false;
    if (a2gspu_faketime_ticking()) *out += a2gspu_emulated_seconds();
    return true;
}

// The calendar fields a clock card shows now: the pinned clock's fields under
// A2GSPU_FAKETIME (frozen, or ticking in emulated time), else the host's local time.
// False only when the host conversion fails.
inline bool a2gspu_clock_fields(struct tm *out) {
    long long pinned = 0;
    const bool is_pinned = a2gspu_pinned_now_unix(&pinned);
    const time_t t = is_pinned ? (time_t)pinned : std::time(nullptr);
#ifdef _WIN32
    return (is_pinned ? gmtime_s(out, &t) : localtime_s(out, &t)) == 0;
#else
    return (is_pinned ? gmtime_r(&t, out) : localtime_r(&t, out)) != nullptr;
#endif
}
