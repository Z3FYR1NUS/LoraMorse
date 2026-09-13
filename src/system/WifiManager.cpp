#include "WifiManager.h"

#include <WiFi.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

void WifiManager::begin(const char* ssid, const char* pass) {
    ssid_ = ssid;
    pass_ = pass;
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(false);  // One explicit retry state machine.
    startAttempt(millis());
}

void WifiManager::startAttempt(uint32_t now) {
    attempting_ = true;
    attemptAt_ = now;
    WiFi.begin(ssid_, pass_);
    Serial.println("[WiFi] Connecting in background");
}

bool WifiManager::update(uint32_t now) {
    if (uint32_t(now - pollAt_) < timing::WIFI_POLL_MS) return false;
    pollAt_ = now;
    bool changed = false;

    const bool isConnected = WiFi.status() == WL_CONNECTED;
    if (isConnected) {
        const IPAddress ip = WiFi.localIP();
        char currentIp[16];
        snprintf(currentIp, sizeof(currentIp), "%u.%u.%u.%u", unsigned(ip[0]),
                 unsigned(ip[1]), unsigned(ip[2]), unsigned(ip[3]));
        if (!connected_ || strcmp(currentIp, ipText_)) {
            connected_ = true;
            attempting_ = false;
            snprintf(ipText_, sizeof(ipText_), "%s", currentIp);
            changed = true;
            Serial.print("[WiFi] IP: ");
            Serial.println(ipText_);
        }
        return changed;
    }

    if (connected_) {
        connected_ = false;
        ipText_[0] = '\0';
        attempting_ = false;
        retryAt_ = now;
        changed = true;
        Serial.println("[WiFi] Connection lost");
    }
    if (attempting_ && uint32_t(now - attemptAt_) >= timing::WIFI_CONNECT_TIMEOUT_MS) {
        WiFi.disconnect(false, false);
        attempting_ = false;
        retryAt_ = now;
        changed = true;
        Serial.println("[WiFi] Attempt timed out; Morse remains available");
    } else if (!attempting_ && uint32_t(now - retryAt_) >= timing::WIFI_RETRY_INTERVAL_MS) {
        startAttempt(now);
        changed = true;
    }
    return changed;
}
