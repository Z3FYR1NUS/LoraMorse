#include "OtaManager.h"

#include <ArduinoOTA.h>

void OtaManager::configure(const char* hostname, const char* password,
                            VoidFn onStart, ProgressFn onProgress, VoidFn onEnd,
                            ErrorFn onError) {
    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.setPassword(password);

    ArduinoOTA.onStart([onStart]() {
        if (onStart) onStart();
    });
    ArduinoOTA.onProgress([onProgress](unsigned int progress, unsigned int total) {
        if (onProgress) onProgress(progress, total);
    });
    ArduinoOTA.onEnd([onEnd]() {
        if (onEnd) onEnd();
    });
    ArduinoOTA.onError([onError](ota_error_t error) {
        if (onError) onError(uint8_t(error));
    });
}

void OtaManager::start() {
    if (started_) ArduinoOTA.end();
    ArduinoOTA.begin();
    started_ = true;
}

void OtaManager::stop() {
    if (started_) ArduinoOTA.end();
    started_ = false;
}

void OtaManager::handle() {
    if (started_) ArduinoOTA.handle();
}
