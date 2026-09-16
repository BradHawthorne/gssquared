#pragma once

#include <SDL3/SDL.h>
#include <cstdio>
#include <ctime>
#include <cstdint>
#include <cstring>

#include "serial_devices/SerialDevice.hpp"
#include "a2gspu_run_kind.hpp"
#include "util/DeviceFrameDispatcher.hpp"
#include "util/Event.hpp"
#include "util/EventQueue.hpp"

/** Status message from FileDevice worker → main. Copied by value into the SPSC ring. */
struct FileCloseStatusMsg {
    enum Type : uint8_t { NONE = 0, CLOSED = 1 };
    Type type = NONE;
    char text[192]{};
};

/**
 * SPSC ring (same shape as SerialQueue / ScreenshotStatusQueue):
 * worker may only send(), main may only get().
 */
class FileCloseStatusQueue {
    constexpr static uint32_t queue_depth = 4;
    constexpr static uint32_t queue_mask = queue_depth - 1;

    FileCloseStatusMsg queue[queue_depth]{};
    uint32_t head = 0;
    uint32_t tail = 0;

public:
    inline bool is_empty() const { return head == tail; }
    inline bool is_full() const { return ((head + 1) & queue_mask) == tail; }

    inline FileCloseStatusMsg get() {
        if (is_empty()) {
            return FileCloseStatusMsg{};
        }
        FileCloseStatusMsg msg = queue[tail];
        tail = (tail + 1) & queue_mask;
        return msg;
    }

    inline bool send(const FileCloseStatusMsg &msg) {
        if (is_full()) {
            return false;
        }
        queue[head] = msg;
        head = (head + 1) & queue_mask;
        return true;
    }
};

class FileDevice : public SerialDevice {
    private:
        FILE *file;
        uint64_t last_write_ticks = 0;
        static constexpr uint64_t idle_close_ms = 10'000;
        char filename[256]{};
        char display_msg_[256]{}; // main-thread only; pointed at by EventQueue OSD events
        FileCloseStatusQueue status_q_;
        EventQueue *event_queue_ = nullptr;
        DeviceFrameDispatcher *frames_ = nullptr;
        size_t frame_handler_id_ = static_cast<size_t>(-1);
        const bool automated_ = a2gspu_automated_run();

        void notify_closed() {
            printf("file %s closed\n", filename);
            FileCloseStatusMsg msg;
            msg.type = FileCloseStatusMsg::CLOSED;
            std::snprintf(msg.text, sizeof(msg.text), "File closed %s", filename);
            // Worker → main: SPSC ring only. Never touch EventQueue here.
            status_q_.send(msg);
        }

        void mark_write() {
            last_write_ticks = SDL_GetTicks();
        }

        /** Drain worker close notifications into EventQueue. Main thread only; never blocks. */
        void poll() {
            if (!event_queue_) {
                return;
            }
            // At most one per frame: EventQueue stores a pointer into display_msg_, and
            // frame_appevent consumes a single event per call.
            FileCloseStatusMsg msg = status_q_.get();
            if (msg.type == FileCloseStatusMsg::NONE) {
                return;
            }
            std::snprintf(display_msg_, sizeof(display_msg_), "%s", msg.text);
            event_queue_->addEvent(new Event(EVENT_SHOW_MESSAGE, 0, display_msg_));
        }

    public:
        FileDevice(EventQueue *event_queue, DeviceFrameDispatcher *frames, const char *port_id)
            : SerialDevice("FileDevice", port_id), event_queue_(event_queue), frames_(frames) {
            file = NULL;
            if (frames_) {
                frame_handler_id_ = frames_->registerHandler([this]() {
                    poll();
                    return true;
                });
            }
        }

        ~FileDevice() {
            if (frames_ && frame_handler_id_ != static_cast<size_t>(-1)) {
                frames_->unregisterHandler(frame_handler_id_);
                frames_ = nullptr;
                frame_handler_id_ = static_cast<size_t>(-1);
            }
            // Ensure thread stops before our members are destroyed
            if (thread) {
                SDL_Log("SerialDevice: %s shutting down", this->name);
                SerialMessage msg = {MESSAGE_SHUTDOWN, 0};
                q_host.send(msg);
                SDL_WaitThread(thread, NULL);
                thread = nullptr;
            }
            close_file(false); // no toast on teardown; console log still useful if a file was open
            event_queue_ = nullptr;
        }

        void open_file() {
            // FORK: an automated run (a2gspu_run_kind.hpp) captures to a STABLE name in its
            // working directory, appended: parallel.out for a parallel port -- the name both
            // trees' parallel card used before the connection framework, and the one
            // consumer checks read -- and GS2.<port>.out otherwise. A name built from the
            // host's clock is an output a harness cannot find, and differs every run.
            if (a2gspu_automated_run()) {
                if (strncmp(port_id, "PARL", 4) == 0) snprintf(filename, sizeof(filename), "parallel.out");
                else snprintf(filename, sizeof(filename), "GS2.%s.out", port_id);
                printf("A2GSPU FILEDEV: port=%s file=%s (automated run: stable name, appended)\n", port_id, filename);
                file = fopen(filename, "a");
                if (file == NULL) {
                    printf("Failed to open file %s\n", filename);
                    filename[0] = '\0';
                    return;
                }
                mark_write();
                return;
            }
            // construct file name from "GS2.{Extension}.{YYYYMMDDHHMMSS}"
            time_t now = time(NULL);
            struct tm *tm_now = localtime(&now);
            char timestamp[20];
            strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S", tm_now);

            snprintf(filename, sizeof(filename), "GS2.%s.%s", port_id, timestamp);
            printf("FileDevice: opening file %s\n", filename);

            file = fopen(filename, "w");
            if (file == NULL) {
                printf("Failed to open file %s\n", filename);
                filename[0] = '\0';
                return;
            }
            mark_write();
        }

        void close_file(bool notify = true) {
            if (file != NULL) {
                fclose(file);
                file = NULL;
                last_write_ticks = 0;
                if (filename[0] != '\0') {
                    if (notify) {
                        notify_closed();
                    } else {
                        printf("file %s closed\n", filename);
                    }
                    filename[0] = '\0';
                }
            }
        }

        void device_loop() override {
            while (true) {
                SDL_Delay(10);

                // FORK: no idle close on an automated run -- when a job "ends" would be
                // decided by host time, and the capture stays one appended file.
                if (file && last_write_ticks != 0 && !automated_ &&
                    (SDL_GetTicks() - last_write_ticks) >= idle_close_ms) {
                    close_file();
                }

                bool wrote = false;
                while (!q_host.is_empty()) {
                    SerialMessage msg = q_host.get();
                    switch (msg.type) {
                        case MESSAGE_SHUTDOWN:
                            return;
                        case MESSAGE_CLOSE:
                            close_file();
                            break;
                        case MESSAGE_DATA:
                            if (!file) open_file();
                            if (file) {
                                fwrite(&msg.data, 1, 1, file);
                                mark_write();
                                wrote = true;
                            }
                            break;
                        default:
                            break;
                    }
                }
                // FORK: flush what this pass wrote. stdio buffers, and nothing
                // else flushes until the idle close 10 s later, so a print job
                // sat invisibly in the buffer and the capture file disagreed
                // with the machine for that long. A printer's byte is gone the
                // moment it is strobed; anything watching the file should see it.
                if (wrote && file) fflush(file);
            }
        }
};
