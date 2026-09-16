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
#include <cstdlib>
#include <ctime>

// The pinned instant, or false when A2GSPU_FAKETIME is unset or empty.
inline bool a2gspu_faketime_unix(long long *out) {
    const char *e = std::getenv("A2GSPU_FAKETIME");
    if (!e || !*e) return false;
    *out = std::strtoll(e, nullptr, 10);
    return true;
}

// The calendar fields a clock card shows now: the pinned instant's fields under
// A2GSPU_FAKETIME (frozen, as the RTC is), else the host's local time. False only
// when the host conversion fails.
inline bool a2gspu_clock_fields(struct tm *out) {
    long long pinned = 0;
    const bool is_pinned = a2gspu_faketime_unix(&pinned);
    const time_t t = is_pinned ? (time_t)pinned : std::time(nullptr);
#ifdef _WIN32
    return (is_pinned ? gmtime_s(out, &t) : localtime_s(out, &t)) == 0;
#else
    return (is_pinned ? gmtime_r(&t, out) : localtime_r(&t, out)) != nullptr;
#endif
}
