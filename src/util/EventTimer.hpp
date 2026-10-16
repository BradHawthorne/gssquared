#pragma once

#include <vector>
#include <cstdint>

class NClockII;  // forward declare instead of include

class EventTimer {
public:
    struct Event {
        uint64_t triggerCycles;
        void (*triggerCallback)(uint64_t, void*);
        uint64_t instanceID;
        void* userData;
        const char *observation_owner=nullptr;
        const char *observation_source=nullptr;
    };
    NClockII *clock;
    uint64_t next_event_cycle = 0;
    EventTimer(NClockII *clock = nullptr) { this->clock = clock; }
    ~EventTimer();

    // Optional labels must have static lifetime; they do not affect scheduling.
    void scheduleEvent(uint64_t triggerCycles, void (*callback)(uint64_t, void*), uint64_t instanceID, void* userData = nullptr,
                       const char *observation_owner=nullptr,const char *observation_source=nullptr);
    void processEvents(uint64_t currentCycles);
    void cancelEvents(uint64_t instanceID);
    bool hasPendingEvents() const;
    uint64_t getNextEventCycle() const;
    inline bool isEventPassed(uint64_t currentCycles) { return currentCycles >= next_event_cycle; }
    void set_clock(NClockII *clock) { this->clock = clock; }
    // Observation copies are sorted separately; the scheduling heap is untouched.
    std::vector<Event> observation_events() const;
    const char *observation_domain = "undeclared";
    
private:
    std::vector<Event> events;
    void updateNextEventCycle();
};
