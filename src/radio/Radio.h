#pragma once
// Radio -- thin wrapper around sandeepmistry/LoRa.
//
// The original sketch did:
//     #define private public
//     #include <LoRa.h>
//     #undef private
// to reach into the library's internals. Every call actually used
// (begin, setPins, beginPacket, endPacket, write, parsePacket, available,
// read, isTransmitting, idle, packetRssi) is part of the library's public
// API, so the hack was unnecessary and is removed here. Poking at a
// library's private state is fragile across library versions and defeats
// the point of it being encapsulated in the first place.
//
// This wrapper also makes the radio's on-air parameters explicit instead of
// relying on library defaults, and adds SNR alongside the RSSI the original
// UI already showed.

#include <stdint.h>

class Radio {
public:
    bool begin();

    bool beginPacket();
    // Returns true if the full frame was queued and transmission started.
    bool sendPacket(const uint8_t* data, size_t len);
    bool isTransmitting();
    void idle();

    // Non-blocking receive poll. Returns the number of bytes available to
    // read (0 if nothing arrived), mirroring LoRa.parsePacket().
    int poll();
    int available();
    int read();

    int lastRssi() const;
    float lastSnr() const;

    bool ready() const { return ready_; }

private:
    bool ready_ = false;
};
