/**
 * buslogunit: unit checks of src/a2gspu_buslog.hpp that need no machine.
 *
 *   buslogunit           cap clamping and the one-access-per-cycle counter
 *   buslogunit_assert    the same header built with A2_BUSLOG_ASSERT (and
 *                        without NDEBUG): a second access pended in one cycle
 *                        must stop the program with an assertion (ctest
 *                        matches the message)
 *
 * Exit status: 0 all checks pass, 1 a check failed.
 */
#if defined(BUSLOGUNIT_ASSERT)
#undef NDEBUG
#define A2_BUSLOG_ASSERT 1
#else
#undef A2_BUSLOG_ASSERT
#endif
#include "a2gspu_buslog.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static int failures = 0;
static void check(bool ok, const char *what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

int main() {
#if defined(BUSLOGUNIT_ASSERT)
#if defined(_WIN32) && (defined(_MSC_VER) || defined(_UCRT))
    // Report the assertion on stderr and exit; no error-reporting dialog.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    a2buslog::mark(16);
    a2buslog::pend(0x001000, 0x11, a2buslog::K_READ);
    a2buslog::pend(0x001001, 0x22, a2buslog::K_READ);   // must assert
    std::printf("FAIL a second pend in one cycle did not assert\n");
    return 1;
#else
    // Cap: default, clamped ceiling, explicit small cap.
    a2buslog::mark();
    check(a2buslog::g_cap == a2buslog::kDefaultCap, "mark() uses the default cap");
    a2buslog::mark(a2buslog::kMaxCap * 4);
    check(a2buslog::g_cap == a2buslog::kMaxCap, "a cap above kMaxCap is clamped to kMaxCap");
    check(a2buslog::kMaxCap * sizeof(a2buslog::Entry) == (size_t)512 << 20, "kMaxCap records are 512 MiB");
    check(a2buslog::kDefaultCap * sizeof(a2buslog::Entry) == (size_t)32 << 20, "the default cap is 32 MiB");

    // One access per cycle: no overlap.
    a2buslog::mark(16);
    a2buslog::pend(0x001000, 0x11, a2buslog::K_FETCH);
    a2buslog::end_cycle(0);
    a2buslog::end_cycle(1);   // internal cycle
    a2buslog::pend(0x001001, 0x22, a2buslog::K_READ);
    a2buslog::end_cycle(2);
    check(a2buslog::g_overlap == 0 && a2buslog::g_log.size() == 3, "one access per cycle counts no overlap");

    // Two accesses pended in one cycle: counted, the later one recorded.
    a2buslog::pend(0x002000, 0x33, a2buslog::K_READ);
    a2buslog::pend(0x002001, 0x44, a2buslog::K_WRITE);
    a2buslog::end_cycle(3);
    check(a2buslog::g_overlap == 1, "a second pend in one cycle is counted");
    const a2buslog::Entry &e = a2buslog::g_log.back();
    check(e.addr == 0x002001 && e.value == 0x44 && e.kind == a2buslog::K_WRITE, "the later access is the one recorded");
    check(a2buslog::g_log.size() == 4, "still one record per cycle");

    char buf[512];
    a2buslog::format_report(buf, sizeof buf);
    const char *tail = std::strstr(buf, " overlap=");
    check(tail && !std::strcmp(tail, " overlap=1"), "the report ends with overlap=1");
    std::printf("  %s\n", buf);

    a2buslog::mark(16);
    check(a2buslog::g_overlap == 0, "mark clears the overlap count");
    a2buslog::format_report(buf, sizeof buf);
    tail = std::strstr(buf, " overlap=");
    check(tail && !std::strcmp(tail, " overlap=0"), "a fresh report ends with overlap=0");

    std::printf("%s\n", failures ? "buslogunit: FAILED" : "buslogunit: all checks pass");
    return failures ? 1 : 0;
#endif
}
