#pragma once
#include <cstdint>
// ============================================================================
// house_fnv.hpp — THE one definition of the house FNV-1a-64 constants used by
// every SHR/bus golden + the bidirectional producer<->consumer oracle.
//
// WARNING: this is a TRUNCATED, NON-CANONICAL FNV-1a-64 offset basis. Canonical
// FNV-1a-64 is 14695981039346656037ULL; this is 1469598103934665603ULL (the
// canonical value with the last two digits dropped — a historical choice, now
// load-bearing). Changing it, "fixing" it toward canonical, or mistyping a
// single digit silently invalidates EVERY golden with NO compile error. It is
// defined ONCE here and referenced everywhere so the value cannot drift across
// the 4 sites (bus_trace.hpp, slot_bus.hpp, mmu_state_trace.hpp, gs2.cpp).
// ============================================================================
inline constexpr uint64_t HOUSE_FNV_BASIS = 1469598103934665603ULL;
inline constexpr uint64_t HOUSE_FNV_PRIME = 1099511628211ULL;
