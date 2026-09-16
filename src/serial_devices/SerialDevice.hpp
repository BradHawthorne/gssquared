#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <SDL3/SDL.h>

enum serial_message_type_t {
    MESSAGE_NONE = 0,
    MESSAGE_DATA,
    MESSAGE_BREAK_ON,
    MESSAGE_BREAK_OFF,
    MESSAGE_CLOSE,
    MESSAGE_SHUTDOWN,
    MESSAGE_LINE,
    // FORK: modem lines. MODEM_STATUS travels device -> host (data bit 0 DCD, bit 1 CTS,
    // bit 2 RI; 1 = asserted); MODEM_CTL travels host -> device (bit 0 DTR, bit 1 RTS).
    // BREAK_ON/BREAK_OFF go either way. A device that ignores them changes nothing.
    MESSAGE_MODEM_STATUS,
    MESSAGE_MODEM_CTL,
};

struct SerialMessage {
    serial_message_type_t type;
    uint64_t data;
};

/*
 * FORK: a single-producer / single-consumer ring the two threads can share.
 * head and tail were plain uint32_t read and written from the emulator thread and a
 * device thread at once (a data race). They are atomics now, loaded and stored in the
 * SPSC order. A send into a full ring is no longer silent: it is counted, and the first
 * drop and every power-of-two after it is reported with the queue's label.
 */
class SerialQueue {
    constexpr static uint32_t queue_depth = 128; // must be a power of 2!!
    constexpr static uint32_t queue_mask = queue_depth - 1;

    SerialMessage queue[queue_depth];
    std::atomic<uint32_t> head{0};
    std::atomic<uint32_t> tail{0};
    std::atomic<uint64_t> dropped_{0};
    char label_[48] = "serial queue";

    public:
        SerialQueue() = default;
        ~SerialQueue() = default;

        void set_label(const char *port_id, const char *direction) {
            snprintf(label_, sizeof label_, "%s %s", port_id ? port_id : "?", direction);
        }

        inline bool is_empty() const {
            return head.load(std::memory_order_acquire) == tail.load(std::memory_order_acquire);
        }
        inline SerialMessage get() {
            const uint32_t t = tail.load(std::memory_order_relaxed);
            if (t == head.load(std::memory_order_acquire)) {
                return SerialMessage{MESSAGE_NONE, 0};
            }
            SerialMessage msg = queue[t];
            tail.store((t + 1) & queue_mask, std::memory_order_release);
            return msg;
        }

        inline bool is_full() const {
            return ((head.load(std::memory_order_relaxed) + 1) & queue_mask) == tail.load(std::memory_order_acquire);
        }
        // Enqueue without counting a failure (control messages retry; see SerialDevice::stop).
        inline bool try_send(SerialMessage msg) {
            const uint32_t h = head.load(std::memory_order_relaxed);
            const uint32_t next = (h + 1) & queue_mask;
            if (next == tail.load(std::memory_order_acquire)) {
                return false;
            }
            queue[h] = msg;
            head.store(next, std::memory_order_release);
            return true;
        }
        inline bool send(SerialMessage msg) {
            if (try_send(msg)) return true;
            const uint64_t n = ++dropped_;
            if (n == 1 || (n & (n - 1)) == 0) {
                fprintf(stderr, "Serial: %s queue full -- a message was DROPPED (%llu so far)\n", label_,
                        (unsigned long long)n);
            }
            return false;
        }
        inline uint64_t get_count() const {
            return (head.load(std::memory_order_acquire) - tail.load(std::memory_order_acquire)) & queue_mask;
        }
        inline uint32_t space() const {
            return (queue_depth - 1) - static_cast<uint32_t>(get_count());
        }
        inline uint64_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
};

class SerialDevice {
    protected:
        const char *name;
        const char *port_id;
        SDL_Thread *thread = nullptr;

    public:
        SerialQueue q_host; // host -> dev queue
        SerialQueue q_dev;  // dev -> host queue

        // FORK: the constructor no longer starts the thread. It did, from the BASE
        // constructor, so the thread could call device_loop() before the derived object
        // existed. Whoever creates a device calls start() once it is fully built.
        SerialDevice(const char *name, const char *port_id);
        virtual ~SerialDevice();

        void start();
        // Send SHUTDOWN (retrying while the ring is full -- a dropped SHUTDOWN used to
        // hang SDL_WaitThread forever) and join. Idempotent; every destructor calls it
        // before its own members go away.
        void stop();

        /*
           This method only exits when it receives a SHUTDOWN message. Otherwise
           processes in a loop forever.
           Must ONLY q_host->get() and q_dev->send() to prevent race conditions.
        */

        const char *get_name() { return name; }
        virtual void device_loop() = 0;
};
