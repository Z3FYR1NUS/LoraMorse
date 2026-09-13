#include "App.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
#include <stdio.h>
#include <string.h>

#include "secrets.h"

namespace {

bool hexNibble(char c, uint8_t* nibble) {
    if (c >= '0' && c <= '9') { *nibble = uint8_t(c - '0'); return true; }
    if (c >= 'a' && c <= 'f') { *nibble = uint8_t(c - 'a' + 10); return true; }
    if (c >= 'A' && c <= 'F') { *nibble = uint8_t(c - 'A' + 10); return true; }
    return false;
}

// Halts with a clear serial message on any malformed key rather than
// silently running with a wrong/zeroed key -- that would look like "it
// works" while actually sending plaintext wrapped in a broken cipher.
bool decodeHexKey(const char* hex, uint8_t out[Crypto::KEY_LEN]) {
    if (strlen(hex) != Crypto::KEY_LEN * 2) return false;
    for (size_t i = 0; i < Crypto::KEY_LEN; ++i) {
        uint8_t hi, lo;
        if (!hexNibble(hex[i * 2], &hi) || !hexNibble(hex[i * 2 + 1], &lo)) return false;
        out[i] = uint8_t((hi << 4) | lo);
    }
    return true;
}

[[noreturn]] void fatal(const char* message) {
    Serial.println(message);
    while (true) delay(1000);
}

}  // namespace

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void App::loadSecrets() {
    uint8_t key[Crypto::KEY_LEN];
    if (!decodeHexKey(SECRET_PSK_HEX, key)) {
        fatal("[FATAL] SECRET_PSK_HEX must be exactly 32 hex characters (16 bytes). "
              "Generate one with: openssl rand -hex 16");
    }
    crypto_.begin(key);

    static_assert(SECRET_THIS_DEVICE_ID != 0, "SECRET_THIS_DEVICE_ID must be non-zero");
    static_assert(SECRET_PEER_DEVICE_ID != 0, "SECRET_PEER_DEVICE_ID must be non-zero");
    static_assert(SECRET_THIS_DEVICE_ID != SECRET_PEER_DEVICE_ID,
                  "SECRET_THIS_DEVICE_ID and SECRET_PEER_DEVICE_ID must differ");
    thisDeviceId_ = SECRET_THIS_DEVICE_ID;
    peerDeviceId_ = SECRET_PEER_DEVICE_ID;

    if (strlen(SECRET_OTA_PASSWORD) == 0) {
        fatal("[FATAL] SECRET_OTA_PASSWORD must not be empty -- an empty OTA password "
              "accepts firmware from anyone on the network.");
    }
}

void App::configureOta() {
    ota_.configure(
        app_cfg::OTA_HOSTNAME, SECRET_OTA_PASSWORD,
        /*onStart=*/[this]() {
            otaActive_ = true;
            otaPercent_ = 0;
            stopOutputs();
            txCount_ = txHead_ = txTail_ = 0;
            txState_ = TxState::IDLE;
            outgoing_.clear();
            incoming_.clear();
            rxDiscarding_ = false;
            if (radioReady_) radio_.idle();
            display_.showOtaScreen("UPDATING FIRMWARE", 0);
            Serial.println("[OTA] Starting");
        },
        /*onProgress=*/[this](unsigned progress, unsigned total) {
            if (!total) return;
            uint32_t percent = uint32_t(uint64_t(progress) * 100ULL / total);
            if (percent > 100) percent = 100;
            if (percent == otaPercent_) return;
            otaPercent_ = uint8_t(percent);
            display_.showOtaScreen("UPDATING FIRMWARE", otaPercent_);
        },
        /*onEnd=*/[this]() {
            otaPercent_ = 100;
            display_.showOtaScreen("UPDATE COMPLETE", 100);
            Serial.println("[OTA] Complete; restarting");
            // Leave otaActive_ true: the library reboots after this callback.
        },
        /*onError=*/[this](uint8_t error) {
            const bool interrupted = otaActive_;
            otaActive_ = false;
            if (interrupted) {
                resetInputs(millis());
                // Restore the peer's letter boundary in case OTA interrupted mid-character.
                if (radioReady_) queueToken('/', '\0', false);
            }
            char message[32];
            snprintf(message, sizeof(message), "OTA ERROR %u", unsigned(error));
            setStatus(message);
            Serial.println(message);
        });
}

void App::begin() {
    Serial.begin(115200);
    loadSecrets();

    pinMode(pins::KEY_INPUT, INPUT_PULLUP);
    pinMode(pins::CONTROL_INPUT, INPUT_PULLUP);
    pinMode(pins::BUZZER, OUTPUT);
    pinMode(pins::TX_LED, OUTPUT);
    pinMode(pins::RX_LED, OUTPUT);
    stopOutputs();
    resetInputs(millis());

    display_.begin();

    seqStore_.begin();
    radioReady_ = radio_.begin();

    configureOta();
    wifi_.begin(SECRET_WIFI_SSID, SECRET_WIFI_PASS);

    resetInputs(millis());
    setStatus(radioReady_ ? "READY - WIFI CONNECTING" : "RADIO INIT FAILED");
    footerAt_ = millis();
    Serial.println(radioReady_ ? "[LoRa] Ready" : "[LoRa] Initialization failed");
}

// ---------------------------------------------------------------------------
// Morse tree text helpers
// ---------------------------------------------------------------------------

void App::setStatus(const char* text) {
    snprintf(statusText_, sizeof(statusText_), "%s", text);
    statusAt_ = millis();
    statusActive_ = true;
    display_.markDirty();
}

void App::letterStatus(const char* prefix, char letter) {
    char message[32];
    snprintf(message, sizeof(message), "%s %c", prefix, letter);
    setStatus(message);
}

void App::appendReceivedLetter(char letter) {
    if (receivedLength_ == app_cfg::LOG_CAPACITY) {
        memmove(receivedText_, receivedText_ + 1, app_cfg::LOG_CAPACITY - 1);
        --receivedLength_;
    }
    receivedText_[receivedLength_++] = letter;
    receivedText_[receivedLength_] = '\0';
    display_.markDirty();
}

bool App::queueToken(char token, char decoded, bool reserveDelimiter) {
    if (!radioReady_) { setStatus("RADIO OFFLINE"); return false; }
    // A mark always leaves room for its terminating slash.
    const uint8_t limit = app_cfg::TX_QUEUE_SIZE - (reserveDelimiter ? 1 : 0);
    if (txCount_ >= limit) { setStatus("TX QUEUE FULL"); return false; }
    txQueue_[txTail_] = {token, decoded};
    txTail_ = uint8_t((txTail_ + 1) % app_cfg::TX_QUEUE_SIZE);
    ++txCount_;
    display_.markDirty();
    return true;
}

bool App::finishOutgoingCharacter() {
    if (!outgoing_.length) return true;
    const char decoded = morse::decode(outgoing_);
    if (!queueToken('/', decoded, false)) return false;
    outgoing_.clear();
    letterStatus("QUEUED", decoded);
    return true;
}

void App::sendWordSpace() {
    if (!radioReady_) { setStatus("RADIO OFFLINE"); return; }
    const uint8_t needed = outgoing_.length ? 2 : 1;
    if (app_cfg::TX_QUEUE_SIZE - txCount_ < needed) { setStatus("TX QUEUE FULL"); return; }
    if (!finishOutgoingCharacter()) return;
    if (queueToken(' ', ' ', false)) setStatus("SPACE QUEUED");
}

void App::clearBuffers() {
    // Close the remote letter before discarding local TX state.
    if (!finishOutgoingCharacter()) return;
    rxDiscarding_ = rxDiscarding_ || incoming_.length != 0;
    incoming_.clear();
    receivedLength_ = 0;
    receivedText_[0] = '\0';
    lastTxChar_ = lastRxChar_ = '\0';
    hasRssi_ = false;
    if (key_.down() || key_.raw()) ignoreKeyUntilRelease_ = true;
    setStatus("BUFFERS CLEARED");
}

void App::queueBeep(char token) {
    if (beepCount_ == app_cfg::BEEP_QUEUE_SIZE) return;  // Audio never blocks RF processing.
    beepQueue_[beepTail_] = token == '-' ? timing::BEEP_DASH_MS : timing::BEEP_DOT_MS;
    beepTail_ = uint8_t((beepTail_ + 1) % app_cfg::BEEP_QUEUE_SIZE);
    ++beepCount_;
}

void App::finishReceivedCharacter() {
    if (!rxDiscarding_ && incoming_.length) {
        lastRxChar_ = morse::decode(incoming_);
        appendReceivedLetter(lastRxChar_);
        letterStatus("RX", lastRxChar_);
    }
    incoming_.clear();
    rxDiscarding_ = false;
}

void App::handleReceivedToken(char token, uint32_t now) {
    if (token != '.' && token != '-' && token != '/' && token != ' ') return;
    rxPulse_ = true;
    rxPulseAt_ = now;
    digitalWrite(pins::RX_LED, HIGH);
    if (token == '.' || token == '-') {
        lastRxMarkAt_ = now;
        if (!rxDiscarding_) incoming_.append(token);
        queueBeep(token);
        setStatus(token == '.' ? "RX DOT" : "RX DASH");
    } else {
        // Space is also a boundary if the preceding slash was lost.
        finishReceivedCharacter();
        if (token == ' ') {
            lastRxChar_ = ' ';
            appendReceivedLetter(' ');
            setStatus("RX SPACE");
        }
    }
    display_.markDirty();
}

// ---------------------------------------------------------------------------
// Radio: framing helpers
// ---------------------------------------------------------------------------

void App::randomNonce(uint8_t out[Crypto::NONCE_LEN]) {
    // esp_random() is the ESP32 hardware TRNG. 12 bytes needs three 32-bit
    // draws; the fourth byte of the last draw is unused.
    uint32_t r0 = esp_random(), r1 = esp_random(), r2 = esp_random();
    memcpy(out, &r0, 4);
    memcpy(out + 4, &r1, 4);
    memcpy(out + 8, &r2, 4);
}

void App::sendFramed(PacketType type, uint8_t dst, uint32_t seq, const uint8_t* payload,
                      uint8_t payloadLen) {
    uint8_t nonce[Crypto::NONCE_LEN];
    randomNonce(nonce);
    uint8_t buf[Packet::MAX_WIRE_LEN];
    const size_t len = Packet::encode(crypto_, type, thisDeviceId_, dst, seq, nonce, payload,
                                       payloadLen, buf, sizeof(buf));
    if (!len || !radio_.beginPacket()) return;
    radio_.sendPacket(buf, len);
    digitalWrite(pins::TX_LED, HIGH);
    txPulse_ = true;
    txPulseAt_ = millis();
}

void App::beginTxFromQueue(uint32_t now) {
    // Peek, don't pop yet: if the radio can't start a transmission right
    // now (e.g. mid-receive), the item must stay queued and be retried next
    // loop rather than silently dropped.
    if (!radio_.beginPacket()) {
        setStatus("TX BUSY");
        return;
    }

    const QueuedToken item = txQueue_[txHead_];
    const uint32_t seq = seqStore_.nextTxSeq();
    uint8_t nonce[Crypto::NONCE_LEN];
    randomNonce(nonce);
    const uint8_t payload = uint8_t(item.token);
    const size_t len = Packet::encode(crypto_, PacketType::DATA, thisDeviceId_, peerDeviceId_,
                                       seq, nonce, &payload, 1, framedPacket_,
                                       sizeof(framedPacket_));
    if (!len) {
        setStatus("TX ENCODE FAILED");
        return;
    }
    radio_.sendPacket(framedPacket_, len);
    framedPacketLen_ = len;

    // Only now that the frame is actually on its way out does the item
    // leave the queue.
    txHead_ = uint8_t((txHead_ + 1) % app_cfg::TX_QUEUE_SIZE);
    --txCount_;

    inFlight_ = item;
    currentSeq_ = seq;
    retryCount_ = 0;
    txState_ = TxState::SENDING;
    txStartedAt_ = now;
    lastSendAt_ = now;
    ++diag_.txAttempts;

    digitalWrite(pins::TX_LED, HIGH);
    txPulse_ = true;
    txPulseAt_ = now;
    display_.markDirty();
}

// ---------------------------------------------------------------------------
// Per-loop services
// ---------------------------------------------------------------------------

void App::serviceInputs(uint32_t now) {
    key_.update(now);
    control_.update(now);
    if (key_.pressed() && !ignoreKeyUntilRelease_) {
        // Catch a new press that starts just after the letter-gap boundary.
        if (outgoing_.length && uint32_t(key_.edgeAt() - lastMarkAt_) >= timing::CHARACTER_PAUSE_MS)
            finishOutgoingCharacter();
        keyStartedAt_ = key_.edgeAt();
        display_.markDirty();
    }
    if (key_.released()) {
        if (ignoreKeyUntilRelease_) {
            ignoreKeyUntilRelease_ = false;
        } else {
            const uint32_t duration = key_.edgeAt() - keyStartedAt_;
            const char mark = duration < timing::DOT_DASH_SPLIT_MS ? '.' : '-';
            if (queueToken(mark, '\0', true)) {
                outgoing_.append(mark);
                lastMarkAt_ = key_.edgeAt();
                setStatus(outgoing_.overflow ? "TOO MANY MARKS" :
                          (mark == '.' ? "TX DOT" : "TX DASH"));
            }
        }
        display_.markDirty();
    }

    if (control_.pressed()) {
        controlStartedAt_ = control_.edgeAt();
        controlHandled_ = false;
        display_.markDirty();
    }
    if (control_.down() && !controlHandled_ &&
        uint32_t(now - controlStartedAt_) >= timing::CONTROL_HOLD_MS) {
        controlHandled_ = true;
        if (key_.down() || key_.raw()) setStatus("RELEASE KEY FIRST");
        else sendWordSpace();
    }
    if (control_.released()) {
        if (!controlHandled_) clearBuffers();
        display_.markDirty();
    }
    // raw() prevents finalizing during a press that is still being debounced.
    if (!key_.down() && !key_.raw() && outgoing_.length &&
        uint32_t(now - lastMarkAt_) >= timing::CHARACTER_PAUSE_MS) {
        finishOutgoingCharacter();
    }
}

void App::serviceRadioRx(uint32_t now) {
    // parsePacket()/poll() changes radio mode: never call it while an async
    // TX started with sendPacket() is still running.
    if (txState_ == TxState::SENDING && radio_.isTransmitting()) return;

    const int packetSize = radio_.poll();
    if (packetSize <= 0) return;

    lastRssi_ = radio_.lastRssi();
    lastSnr_ = radio_.lastSnr();
    hasRssi_ = true;

    uint8_t buf[Packet::MAX_WIRE_LEN];
    int received = 0;
    while (radio_.available() && received < int(sizeof(buf))) {
        const int value = radio_.read();
        if (value >= 0) buf[received++] = uint8_t(value);
    }

    PacketHeader header;
    uint8_t payload[MAX_PAYLOAD_LEN] = {0};
    if (!Packet::decode(crypto_, buf, size_t(received), &header, payload)) {
        ++diag_.badPackets;
        setStatus("BAD/UNAUTH PACKET");
        display_.markDirty();
        return;
    }
    if (header.dst != thisDeviceId_) return;  // Not addressed to us; ignore silently.

    if (header.type == PacketType::ACK) {
        if (txState_ == TxState::WAIT_ACK && header.src == peerDeviceId_ &&
            header.seq == currentSeq_) {
            ++diag_.delivered;
            txState_ = TxState::IDLE;
            if (inFlight_.token == '/' && inFlight_.decoded) {
                lastTxChar_ = inFlight_.decoded;
                letterStatus("DELIVERED", lastTxChar_);
            } else if (inFlight_.token == ' ') {
                lastTxChar_ = ' ';
                setStatus("SPACE DELIVERED");
            }
            display_.markDirty();
        }
        // Stray/late/duplicate ACKs are ignored -- the retry logic already
        // accounts for a lost ACK by resending the DATA packet.
        return;
    }

    // DATA packet.
    const ReplayGuard::Verdict verdict = replay_.check(header.src, header.seq);
    if (verdict == ReplayGuard::Verdict::REPLAY || verdict == ReplayGuard::Verdict::TABLE_FULL) {
        ++diag_.replayedPackets;
        setStatus("REPLAY/UNKNOWN PEER");
        display_.markDirty();
        return;
    }
    if (verdict == ReplayGuard::Verdict::NEW && header.payloadLen == 1) {
        handleReceivedToken(char(payload[0]), now);
    }
    // NEW or DUPLICATE: (re-)acknowledge. Re-ACKing a duplicate helps the
    // sender when it was our previous ACK that got lost, without
    // reprocessing a token we've already decoded.
    sendFramed(PacketType::ACK, header.src, header.seq, nullptr, 0);
    ++diag_.acksSent;
}

void App::serviceRadioTx(uint32_t now) {
    if (txState_ == TxState::SENDING) {
        if (radio_.isTransmitting()) {
            if (uint32_t(now - txStartedAt_) >= link_cfg::ACK_TIMEOUT_MS) {
                // The radio itself appears stuck transmitting; treat like a
                // failed attempt rather than hanging forever.
                radio_.idle();
                txState_ = TxState::IDLE;
                ++diag_.failed;
                setStatus("TX STUCK");
            }
            return;
        }
        txState_ = TxState::WAIT_ACK;
        lastSendAt_ = now;
        return;
    }

    if (txState_ == TxState::WAIT_ACK) {
        const uint32_t timeout =
            link_cfg::ACK_TIMEOUT_MS + uint32_t(retryCount_) * link_cfg::RETRY_BACKOFF_MS;
        if (uint32_t(now - lastSendAt_) < timeout) return;
        if (retryCount_ >= link_cfg::MAX_RETRIES) {
            txState_ = TxState::IDLE;
            ++diag_.failed;
            if (inFlight_.token == '/' && inFlight_.decoded) letterStatus("FAILED", inFlight_.decoded);
            else setStatus("DELIVERY FAILED");
            display_.markDirty();
            return;
        }
        // Retransmit the exact same frame (same seq/nonce) -- this is a
        // resend of one logical message, not a new packet, so the receiver's
        // replay guard correctly treats a second copy as a DUPLICATE rather
        // than rejecting it outright.
        if (!radio_.beginPacket()) return;
        radio_.sendPacket(framedPacket_, framedPacketLen_);
        ++retryCount_;
        ++diag_.retries;
        lastSendAt_ = now;
        txState_ = TxState::SENDING;
        txStartedAt_ = now;
        digitalWrite(pins::TX_LED, HIGH);
        txPulse_ = true;
        txPulseAt_ = now;
        char msg[16];
        snprintf(msg, sizeof(msg), "RETRY %u", unsigned(retryCount_));
        setStatus(msg);
        return;
    }

    // IDLE: start the next queued item, if any and the radio isn't mid-RX.
    if (!txCount_) return;
    beginTxFromQueue(now);
}

void App::resetInputs(uint32_t now) {
    key_.begin(now);
    control_.begin(now);
    ignoreKeyUntilRelease_ = key_.down();
    controlHandled_ = control_.down();
}

void App::stopOutputs() {
    txPulse_ = rxPulse_ = beepOn_ = beepGap_ = buzzerHigh_ = false;
    beepCount_ = beepHead_ = beepTail_ = 0;
    digitalWrite(pins::TX_LED, LOW);
    digitalWrite(pins::RX_LED, LOW);
    digitalWrite(pins::BUZZER, LOW);
}

void App::serviceOutputs(uint32_t now) {
    if (txPulse_ && uint32_t(now - txPulseAt_) >= timing::TX_FLASH_MS &&
        txState_ != TxState::SENDING) {
        txPulse_ = false;
        digitalWrite(pins::TX_LED, LOW);
        display_.markDirty();
    }
    if (rxPulse_ && uint32_t(now - rxPulseAt_) >= timing::RX_FLASH_MS) {
        rxPulse_ = false;
        digitalWrite(pins::RX_LED, LOW);
        display_.markDirty();
    }
    if (beepOn_ && uint32_t(now - beepAt_) >= beepDuration_) {
        beepOn_ = false;
        beepGap_ = true;
        beepAt_ = now;
    }
    if (beepGap_ && uint32_t(now - beepAt_) >= timing::BEEP_GAP_MS) beepGap_ = false;
    if (!beepOn_ && !beepGap_ && beepCount_) {
        beepDuration_ = beepQueue_[beepHead_];
        beepHead_ = uint8_t((beepHead_ + 1) % app_cfg::BEEP_QUEUE_SIZE);
        --beepCount_;
        beepAt_ = now;
        beepOn_ = true;
    }
    const bool sound = !otaActive_ &&
        (beepOn_ || (timing::KEY_SIDETONE && key_.down() && !ignoreKeyUntilRelease_));
    if (sound != buzzerHigh_) {
        buzzerHigh_ = sound;
        digitalWrite(pins::BUZZER, sound ? HIGH : LOW);
    }
}

void App::serviceTimers(uint32_t now) {
    if (statusActive_ && uint32_t(now - statusAt_) >= timing::STATUS_HOLD_MS) {
        statusActive_ = false;
        display_.markDirty();  // Explicit expiry redraw, even when otherwise idle.
    }
    if (uint32_t(now - footerAt_) >= timing::FOOTER_PAGE_MS) {
        footerAt_ = now;
        footerShowsIp_ = !footerShowsIp_;
        display_.markDirty();
    }
    if ((incoming_.length || rxDiscarding_) &&
        uint32_t(now - lastRxMarkAt_) >= timing::RX_STALE_MS) {
        incoming_.clear();
        rxDiscarding_ = false;
        setStatus("RX GAP / LOST END");
    }
}

void App::composeFooter(uint32_t now, char* out, size_t outCap) const {
    if (!radioReady_) { snprintf(out, outCap, "RADIO OFFLINE - CHECK WIRING"); return; }
    if (control_.down() && !controlHandled_) { snprintf(out, outCap, "HOLD: SPACE / TAP: CLEAR"); return; }
    if (key_.down() && !ignoreKeyUntilRelease_) {
        const uint32_t duration = now - keyStartedAt_;
        snprintf(out, outCap, "%s  %lums", duration < timing::DOT_DASH_SPLIT_MS ? "DOT" : "DASH",
                 static_cast<unsigned long>(duration));
        return;
    }
    if (statusActive_) { snprintf(out, outCap, "%s", statusText_); return; }
    if (txCount_ || txState_ != TxState::IDLE) {
        snprintf(out, outCap, "SENDING  %u QUEUED", unsigned(txCount_));
        return;
    }
    if (footerShowsIp_ && wifi_.connected()) {
        snprintf(out, outCap, "IP %s", wifi_.ipText());
        return;
    }
    snprintf(out, outCap, "CTRL: TAP CLEAR / HOLD SPACE");
}

void App::serviceDisplay(uint32_t now) {
    if (otaActive_) return;
    UiState state;
    state.radioReady = radioReady_;
    state.hasRssi = hasRssi_;
    state.rssi = lastRssi_;
    state.snr = lastSnr_;
    state.wifiConnected = wifi_.connected();
    state.wifiAttempting = wifi_.attempting();
    state.ipText = wifi_.ipText();
    state.outgoing = &outgoing_;
    state.incoming = &incoming_;
    state.lastTxChar = lastTxChar_;
    state.lastRxChar = lastRxChar_;
    state.txCardLit = txPulse_ || (key_.down() && !ignoreKeyUntilRelease_);
    state.rxCardLit = rxPulse_;

    uint32_t elapsed = 0, total = 1;
    bool visible = true;
    if (control_.down() && !controlHandled_) {
        elapsed = now - controlStartedAt_;
        total = timing::CONTROL_HOLD_MS;
    } else if (key_.down() && !ignoreKeyUntilRelease_) {
        elapsed = now - keyStartedAt_;
        total = timing::DOT_DASH_SPLIT_MS;
    } else if (outgoing_.length) {
        elapsed = now - lastMarkAt_;
        total = timing::CHARACTER_PAUSE_MS;
    } else {
        visible = false;
    }
    state.progressVisible = visible;
    state.progressElapsedMs = elapsed;
    state.progressTotalMs = total;

    state.receivedLog = receivedText_;

    static char footer[32];
    composeFooter(now, footer, sizeof(footer));
    state.footerText = footer;

    state.animated = (key_.down() && !ignoreKeyUntilRelease_) || outgoing_.length ||
                      (control_.down() && !controlHandled_) || wifi_.attempting();

    display_.service(now, state);
}

// ---------------------------------------------------------------------------
// Top-level loop
// ---------------------------------------------------------------------------

void App::loop() {
    const uint32_t now = millis();
    if (!otaActive_) {
        serviceInputs(now);
        if (radioReady_) {
            serviceRadioRx(now);
            serviceRadioTx(now);
        }
        serviceOutputs(now);
        serviceTimers(now);
        if (wifi_.update(now)) {
            if (wifi_.connected()) ota_.start();
            else ota_.stop();
            display_.markDirty();
        }
    }
    if (wifi_.connected()) ota_.handle();
    serviceDisplay(now);
    yield();
}
