#include "EventTimer.hpp"
#include <algorithm>
#include <limits>
#include <iostream>
#include "debug.hpp"
#include "a2gspu_events.hpp"
//#include "cpu.hpp"

// Constructor implementation
/* EventTimer::EventTimer(cpu_state *cpu) : cpu(cpu) {
} */

// Destructor implementation 
EventTimer::~EventTimer() = default;

std::vector<EventTimer::Event> EventTimer::observation_events() const {
    auto copy=events;
    std::sort(copy.begin(),copy.end(),[](const Event&a,const Event&b){return a.triggerCycles==b.triggerCycles?a.instanceID<b.instanceID:a.triggerCycles<b.triggerCycles;});
    return copy;
}

static uint64_t timer_event(const char *kind,const char *domain,uint64_t identity,uint64_t due,uint64_t now,const char *owner=nullptr,const char *source=nullptr) {
    if(!a2events::needs("scheduler"))return 0;
    return a2events::publish("scheduler","{\"kind\":"+a2ctrljson::quote(kind)+",\"domain\":"+a2ctrljson::quote(domain)+
        ",\"instance_id\":"+a2ctrljson::quote(std::to_string(identity))+",\"due\":"+a2ctrljson::quote(std::to_string(due))+
        ",\"observed_domain_time\":"+(std::string(kind)=="fired"?a2ctrljson::quote(std::to_string(now)):"null")+
        ",\"owner\":"+(owner?a2ctrljson::quote(owner):"null")+",\"source\":"+(source?a2ctrljson::quote(source):"null")+
        ",\"model\":\"EventTimer transition; instance is queue-local; labels declare callback implementation\"}");
}

inline void EventTimer::updateNextEventCycle() {
    // Update next_event_cycle to the earliest event's trigger time
    next_event_cycle = events.empty() ? std::numeric_limits<uint64_t>::max() : events.front().triggerCycles;
}

// Add a new event to the queue
void EventTimer::scheduleEvent(uint64_t triggerCycles, void (*callback)(uint64_t, void*), uint64_t instanceID, void* userData,const char *observation_owner,const char *observation_source) {
    if (DEBUG(DEBUG_EVENT_TIMER)) std::cout << "scheduleEvent: " << triggerCycles << " InstanceID: " << instanceID << std::endl;
    if (clock && triggerCycles < clock->get_cycles()) {
        std::cout << "scheduleEvent: Event in the past, skipping" << std::endl;
        return;
    }
    // Check if an event with the same instanceID already exists
    auto existingEvent = std::find_if(events.begin(), events.end(), 
        [instanceID](const Event& event) { return event.instanceID == instanceID; });
    
    if (existingEvent != events.end()) {
        // Replace the existing event with the new one
        existingEvent->triggerCycles = triggerCycles;
        existingEvent->triggerCallback = callback;
        existingEvent->userData = userData;
        existingEvent->observation_owner=observation_owner;
        existingEvent->observation_source=observation_source;
        
        // Rebuild the heap after modifying an element
        std::make_heap(events.begin(), events.end(), [](const Event& a, const Event& b) {
            return a.triggerCycles > b.triggerCycles;
        });
    } else {
        // Add a new event to the queue
        Event newEvent{triggerCycles, callback, instanceID, userData,observation_owner,observation_source};
        events.push_back(newEvent);
        std::push_heap(events.begin(), events.end(), [](const Event& a, const Event& b) {
            return a.triggerCycles > b.triggerCycles; // Reverse for priority queue (earliest first)
        });
    }
    updateNextEventCycle();
    timer_event("scheduled",observation_domain,instanceID,triggerCycles,0,observation_owner,observation_source);
}

// Process all events that should trigger by the given cycle count
void EventTimer::processEvents(uint64_t currentCycles) {
    /* std::cout << "processEvents: " << currentCycles << "Front: " << events.front().triggerCycles << std::endl;
    // Print how many events are on the queue
    if (!events.empty()) {
        std::cout << "Events on queue: " << events.size() << std::endl;
    } */
    while (!events.empty() && events.front().triggerCycles <= currentCycles) {
        Event event = events.front();
        std::pop_heap(events.begin(), events.end(), [](const Event& a, const Event& b) {
            return a.triggerCycles > b.triggerCycles;
        });
        events.pop_back();
        if (DEBUG(DEBUG_EVENT_TIMER)) std::cout << "Processing event: " << event.triggerCycles << " InstanceID: " << event.instanceID << std::endl;
        // Call the callback function
        const auto evidence=timer_event("fired",observation_domain,event.instanceID,event.triggerCycles,currentCycles,event.observation_owner,event.observation_source);
        a2events::CauseScope cause(evidence);
        if (event.triggerCallback) {
            event.triggerCallback(event.instanceID, event.userData);
        }
    }
    updateNextEventCycle();
}

// Cancel all events for a specific instance
void EventTimer::cancelEvents(uint64_t instanceID) {
    if(a2events::needs("scheduler"))for(const auto &event:events)if(event.instanceID==instanceID)
        timer_event("cancelled",observation_domain,instanceID,event.triggerCycles,0,event.observation_owner,event.observation_source);
    auto newEnd = std::remove_if(events.begin(), events.end(),
        [instanceID](const Event& event) { return event.instanceID == instanceID; });
    
    if (newEnd != events.end()) {
        events.erase(newEnd, events.end());
        // Rebuild the heap after removing elements
        std::make_heap(events.begin(), events.end(), [](const Event& a, const Event& b) {
            return a.triggerCycles > b.triggerCycles;
        });
    }
    updateNextEventCycle();
}

// Check if there are any pending events
bool EventTimer::hasPendingEvents() const {
    return !events.empty();
}

// Get the cycle count of the next event
uint64_t EventTimer::getNextEventCycle() const {
    return next_event_cycle;
}
