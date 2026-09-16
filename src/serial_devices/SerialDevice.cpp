#include <SDL3/SDL.h>

#include "SerialDevice.hpp"


// This is the main device thread. This is a bare C routine that calls the SerialDevice object.
int SDLCALL SerialDeviceThreadHandler(void *data) {
    SerialDevice *device = (SerialDevice *)data;
    device->device_loop();
    return 0;
}

SerialDevice::SerialDevice(const char *name, const char *port_id) {
    this->name = name ? name : "SerialDevice";
    this->port_id = port_id ? port_id : "UNK";
    q_host.set_label(this->port_id, "host->device");
    q_dev.set_label(this->port_id, "device->host");
}

void SerialDevice::start() {
    if (!thread) {
        thread = SDL_CreateThread(SerialDeviceThreadHandler, name, (void *) this);
    }
}

void SerialDevice::stop() {
    if (!thread) {
        return;
    }
    SDL_Log("SerialDevice: %s shutting down", name);
    // The device thread drains q_host continuously, so a full ring frees within
    // milliseconds -- unless the thread is blocked (a modem dial can take 40 s). It is
    // never detached: the caller destroys the object next, and a thread still running
    // device_loop() would use it after it is gone. A long wait is reported instead.
    const SerialMessage msg = {MESSAGE_SHUTDOWN, 0};
    for (uint32_t ms = 0; !q_host.try_send(msg); ms++) {
        if (ms == 1000 || (ms > 0 && ms % 10000 == 0)) {
            fprintf(stderr, "SerialDevice: %s has not taken SHUTDOWN after %u ms (queue full); still waiting\n",
                    name, (unsigned)ms);
        }
        SDL_Delay(1);
    }
    SDL_WaitThread(thread, NULL);
    thread = nullptr;
}

SerialDevice::~SerialDevice() {
    stop();
}
