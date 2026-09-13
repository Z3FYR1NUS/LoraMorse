#pragma once

#include <stddef.h>
#include <stdint.h>

class Radio {
public:
    bool begin();

    bool beginPacket();
    bool sendPacket(const uint8_t* data, size_t len);
    bool isTransmitting();
    void idle();

    int poll();
    int available();
    int read();

    int lastRssi() const;
    float lastSnr() const;

    bool ready() const {
        return ready_;
    }

private:
    static Radio* instance_;
    static void onTxDoneStatic();

    void onTxDone();

    bool ready_ = false;
    bool transmitting_ = false;
};
