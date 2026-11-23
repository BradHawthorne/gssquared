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
#include <cstdio>
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

// Unpinned wall-clock origin (unix seconds at emulated time zero). Relatched
// when a new machine installs a timesource. The time-of-day the guest reads
// is this origin plus emulated seconds -- never a fresh host time() -- so a
// freeze or halt does not jump the clock AppleTalk QtrSched catch-up uses.
inline long long g_a2gspu_unpinned_origin_unix = 0;
inline bool g_a2gspu_unpinned_origin_latched = false;

inline void a2gspu_timesource_install(uint64_t (*c14m)(void *), void *ctx) {
    g_a2gspu_timesource_c14m = c14m;
    g_a2gspu_timesource_ctx = ctx;
    g_a2gspu_unpinned_origin_latched = false;
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

// The pin as a run's identity prints it: the value, "<value>/tick" when it ticks, or
// "unset". FORK: without the suffix a frozen and a ticking run printed identical PINS
// lines and manifests while their clocks differed.
inline void a2gspu_faketime_field(char *buf, size_t n) {
    const char *e = std::getenv("A2GSPU_FAKETIME");
    if (!e || !*e) { snprintf(buf, n, "unset"); return; }
    snprintf(buf, n, "%s%s", e, a2gspu_faketime_ticking() ? "/tick" : "");
}

// The unix instant every guest-visible clock shows now. Under A2GSPU_FAKETIME
// this is the pin (plus emulated seconds when ticking). Unpinned, the origin is
// host wall-clock latched once (so the Control Panel still shows the time of
// day) and then advances one second per 14,318,180 emulated 14M ticks -- never
// host time. A 2 s LToUDP collect or a rail HALT, with C14M frozen, adds 0.
inline void a2gspu_guest_now_unix(long long *out) {
    if (a2gspu_pinned_now_unix(out)) return;
    if (!g_a2gspu_unpinned_origin_latched) {
        g_a2gspu_unpinned_origin_unix = (long long)std::time(nullptr) - a2gspu_emulated_seconds();
        g_a2gspu_unpinned_origin_latched = true;
    }
    *out = g_a2gspu_unpinned_origin_unix + a2gspu_emulated_seconds();
}

// The calendar fields a clock card shows now: pinned (UTC, no host timezone)
// or the unpinned guest clock converted as local time. False only when the
// conversion fails.
inline bool a2gspu_clock_fields(struct tm *out) {
    long long now = 0;
    a2gspu_guest_now_unix(&now);
    const time_t t = (time_t)now;
    long long pin = 0;
    const bool pinned = a2gspu_faketime_unix(&pin);
#ifdef _WIN32
    return (pinned ? gmtime_s(out, &t) : localtime_s(out, &t)) == 0;
#else
    return (pinned ? gmtime_r(&t, out) : localtime_r(&t, out)) != nullptr;
#endif
}
