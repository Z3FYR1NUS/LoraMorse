#pragma once
// OtaManager -- thin wrapper around ArduinoOTA so App doesn't touch the
// library directly. Callbacks let App own what actually happens on
// start/progress/end/error (silencing outputs, drawing the progress screen,
// etc.) without OtaManager needing to know about the rest of the app.

#include <functional>
#include <stdint.h>

class OtaManager {
public:
    using VoidFn = std::function<void()>;
    using ProgressFn = std::function<void(unsigned progress, unsigned total)>;
    using ErrorFn = std::function<void(uint8_t errorCode)>;

    // `password` must be non-empty: ArduinoOTA with an empty password
    // accepts firmware from anyone who can reach the device on the network.
    // This is enforced by requiring SECRET_OTA_PASSWORD to be set (see
    // include/secrets.example.h) rather than silently allowing "".
    void configure(const char* hostname, const char* password,
                    VoidFn onStart, ProgressFn onProgress, VoidFn onEnd, ErrorFn onError);

    // Call once WiFi has an IP address. Safe to call again after end()+begin()
    // cycling (e.g. on reconnect).
    void start();
    void stop();
    void handle();

private:
    bool started_ = false;
};
