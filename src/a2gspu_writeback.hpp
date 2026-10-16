#pragma once
#include <cstdlib>
#include <string>

// One predicate for the spike-end flush and its observer. These legacy flags
// test presence, including a value of "0"; do not reinterpret them as booleans.
namespace a2writeback {
struct Policy {
    bool canonical_request, legacy_request, canonical_guard, legacy_guard;
    bool requested() const { return canonical_request || legacy_request; }
    bool suppressed() const { return canonical_guard || legacy_guard; }
    bool flush() const { return requested() && !suppressed(); }
    std::string json() const {
        auto bit=[](bool value){return value?"true":"false";};
        return std::string("{\"schema\":\"a2writeback-policy-v1\",\"model\":\"current-environment-presence; spike-end-flush-only\",\"canonical_request\":")+bit(canonical_request)+
            ",\"legacy_request\":"+bit(legacy_request)+",\"canonical_guard\":"+bit(canonical_guard)+",\"legacy_guard\":"+bit(legacy_guard)+
            ",\"requested\":"+bit(requested())+",\"suppressed\":"+bit(suppressed())+",\"flush_at_spike_end\":"+bit(flush())+
            ",\"scope\":\"does not govern explicit save/unmount operations, BRAM or arbitrary device/host endpoints\"}";
    }
};
inline Policy current() {
    return {std::getenv("A2GSPU_WRITEBACK")!=nullptr,std::getenv("WIZ5_WRITEBACK")!=nullptr,
            std::getenv("A2GSPU_NO_WRITEBACK")!=nullptr,std::getenv("WIZ5_NO_WRITEBACK")!=nullptr};
}
}
