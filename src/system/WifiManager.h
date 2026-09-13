#pragma once
// WifiManager -- non-blocking WiFi connect/retry state machine.
// Extracted from the original sketch's maintainWiFi()/startWiFiAttempt()
// with the same behavior.

#include <stdint.h>

class WifiManager {
public:
    void begin(const char* ssid, const char* pass);

    // Call every loop iteration. Returns true if the connected/attempting
    // state changed this call (callers can use this to mark the UI dirty).
    bool update(uint32_t now);

    bool connected() const { return connected_; }
    bool attempting() const { return attempting_; }
    const char* ipText() const { return ipText_[0] ? ipText_ : nullptr; }

private:
    void startAttempt(uint32_t now);

    const char* ssid_ = nullptr;
    const char* pass_ = nullptr;
    bool connected_ = false;
    bool attempting_ = false;
    char ipText_[16] = "";
    uint32_t pollAt_ = 0, attemptAt_ = 0, retryAt_ = 0;
};
