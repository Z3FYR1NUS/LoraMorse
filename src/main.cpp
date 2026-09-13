#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Preferences.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <LoRa.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mbedtls/gcm.h"
#include "esp_system.h"

// ============================================================================
// USER CONFIGURATION (ALL SETTINGS CONSOLIDATED HERE)
// ============================================================================

// Device Addressing ----------------------------------------------------------
constexpr uint16_t DEVICE_ID = 0x0002;
constexpr uint16_t PEER_DEVICE_ID = 0x0001;
// .122 = 0x0001
// .123 = 0x0002
// Cryptographic Key (Must contain exactly 32 hex characters / 128-bit)
constexpr char AES_KEY_HEX[] = "";

// WiFi & Over-The-Air (OTA) Updating ----------------------------------------
const char* WIFI_SSID = "";
const char* WIFI_PASS = "!";
const char* OTA_HOSTNAME = "";
const char* OTA_PASSWORD = "";
constexpr bool ENABLE_OTA = ; //true or false

// LoRa Physical Layer Configuration -----------------------------------------
constexpr long LORA_FREQUENCY = 433000000L;
constexpr long LORA_BANDWIDTH = 125000L;
constexpr int LORA_SPREADING_FACTOR = 7;
constexpr int LORA_CODING_RATE = 5;  // 4/5
constexpr uint8_t LORA_SYNC_WORD = 0x12;
constexpr long LORA_PREAMBLE_LENGTH = 8;
constexpr int LORA_TX_POWER = 17;
constexpr bool LORA_ENABLE_CRC = true;

// Radio Reliability & ARQ Timers --------------------------------------------
constexpr uint32_t ACK_TIMEOUT_MS = 400;    // Retransmit interval if ACK is missed
constexpr uint32_t TX_TIMEOUT_MS = 1200;    // Max time allowed for single packet transmission
constexpr uint8_t MAX_RETRIES = 3;           // Max retransmission attempts

// Local Morse Engine & Batching Timers --------------------------------------
constexpr uint32_t DOT_DASH_SPLIT_MS = 220;    // Key press < 220ms = DOT, >= 220ms = DASH
constexpr uint32_t CHARACTER_PAUSE_MS = 600;   // Idle pause to complete a Morse letter
constexpr uint32_t AUTO_FLUSH_IDLE_MS = 1500;  // Idle pause to flush outbox word over RF
constexpr uint32_t DEBOUNCE_MS = 8;
constexpr uint32_t CONTROL_HOLD_MS = 500;
constexpr bool KEY_SIDETONE = true;

// GPIO Hardware Pin Map -----------------------------------------------------
constexpr int LORA_SCK = 18;
constexpr int LORA_MISO = 19;
constexpr int LORA_MOSI = 23;
constexpr int LORA_CS = 16;
constexpr int LORA_RST = 26;
constexpr int LORA_DIO0 = 25;

constexpr int OLED_SDA = 21;
constexpr int OLED_SCL = 22;

constexpr int BUZZER_PIN = 33;
constexpr int TX_LED_PIN = 32;
constexpr int RX_LED_PIN = 13;

constexpr int KEY_PIN = 27;
constexpr int CONTROL_PIN = 14;

// Display Setup & UI Timers -------------------------------------------------
constexpr uint32_t OLED_I2C_HZ = 400000;
constexpr uint32_t UI_FRAME_MS = 33;           // ~30 FPS refresh rate
constexpr uint32_t STATUS_HOLD_MS = 1500;
constexpr uint32_t FOOTER_PAGE_MS = 5000;

// ============================================================================
// PROTOCOL & DATA LIMITS
// ============================================================================

constexpr uint8_t PACKET_MAGIC = 0xC7;
constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr uint8_t MAX_PAYLOAD_LEN = 16;        // Up to 16 characters per frame payload

enum class PacketType : uint8_t {
    Data = 1,
    Ack = 2
};

constexpr uint8_t NONCE_LEN = 12;
constexpr uint8_t TAG_LEN = 16;
constexpr size_t HEADER_LEN = 24;
constexpr size_t MAX_PACKET_LEN = HEADER_LEN + MAX_PAYLOAD_LEN + TAG_LEN;

constexpr uint8_t SCREEN_W = 128;
constexpr uint8_t SCREEN_H = 64;
constexpr uint8_t MAX_MARKS = 6;
constexpr uint8_t LOG_COLS = 24;
constexpr uint8_t LOG_CAPACITY = 2 * LOG_COLS;
constexpr uint8_t BEEP_QUEUE_SIZE = 32;
constexpr uint8_t TX_QUEUE_SIZE = 8;

static_assert(DEVICE_ID != PEER_DEVICE_ID, "DEVICE_ID and PEER_DEVICE_ID must be unique.");

// ============================================================================
// HARDWARE & STATE STRUCTURES
// ============================================================================

U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);
Preferences preferences;
uint8_t AES_KEY[16] = {};

struct Marks {
    char text[MAX_MARKS + 1] = "";
    uint8_t length = 0;

    void clear() {
        length = 0;
        text[0] = '\0';
    }

    void append(char mark) {
        if (length < MAX_MARKS) {
            text[length++] = mark;
            text[length] = '\0';
        }
    }
};

struct Button {
    int pin;
    bool raw = false;
    bool down = false;
    bool pressed = false;
    bool released = false;
    uint32_t rawAt = 0;
    uint32_t edgeAt = 0;

    explicit Button(int gpio) : pin(gpio) {}

    void begin(uint32_t now) {
        raw = down = (digitalRead(pin) == LOW);
        rawAt = edgeAt = now;
        pressed = released = false;
    }

    void update(uint32_t now) {
        pressed = released = false;
        const bool sample = (digitalRead(pin) == LOW);

        if (sample != raw) {
            raw = sample;
            rawAt = now;
        }

        if (down != raw && uint32_t(now - rawAt) >= DEBOUNCE_MS) {
            down = raw;
            edgeAt = rawAt;
            pressed = down;
            released = !down;
        }
    }
};

struct TxPacket {
    char payload[MAX_PAYLOAD_LEN + 1] = "";
    uint8_t length = 0;
    uint32_t sequence = 0;
};

enum class TxState : uint8_t {
    Idle,
    Transmitting,
    WaitingAck
};

// ============================================================================
// GLOBAL STATE
// ============================================================================

volatile bool g_txDone = true;

Marks outgoingMarks;
Button key(KEY_PIN);
Button control(CONTROL_PIN);

char outboxWordBuffer[MAX_PAYLOAD_LEN + 1] = "";
uint8_t outboxWordLen = 0;

TxPacket txQueue[TX_QUEUE_SIZE] = {};
TxPacket inFlightPacket = {};
uint8_t txHead = 0;
uint8_t txTail = 0;
uint8_t txCount = 0;

uint16_t beepQueue[BEEP_QUEUE_SIZE] = {};
uint8_t beepHead = 0;
uint8_t beepTail = 0;
uint8_t beepCount = 0;

char receivedText[LOG_CAPACITY + 1] = "";
uint8_t receivedLength = 0;

char statusText[48] = "";
char ipText[16] = "";

bool radioReady = false;
bool wifiConnected = false;
bool wifiAttempting = false;
bool otaStarted = false;
bool otaActive = false;
uint8_t otaPercent = 0;

TxState txState = TxState::Idle;
uint8_t retryCount = 0;

bool ackTxBusy = false;
bool ackPending = false;
uint32_t pendingAckSeq = 0;
uint32_t ackTxStartedAt = 0;

uint8_t inFlightFrame[MAX_PACKET_LEN] = {};
size_t inFlightFrameLen = 0;

bool txPulse = false;
bool rxPulse = false;
bool beepOn = false;
bool beepGap = false;
bool buzzerHigh = false;

bool ignoreKeyUntilRelease = false;
bool controlHandled = false;

bool hasRssi = false;
bool hasSnr = false;
int lastRssi = 0;
float lastSnr = 0.0f;

bool statusActive = false;
uint8_t footerPage = 0;
bool screenDirty = true;

uint32_t keyStartedAt = 0;
uint32_t controlStartedAt = 0;
uint32_t lastMarkAt = 0;
uint32_t lastLetterAt = 0;

uint32_t txStartedAt = 0;
uint32_t txPulseAt = 0;
uint32_t rxPulseAt = 0;
uint32_t ackWaitStartedAt = 0;

uint32_t beepAt = 0;
uint32_t beepDuration = 0;

uint32_t statusAt = 0;
uint32_t footerAt = 0;
uint32_t frameAt = 0;

uint32_t wifiPollAt = 0;
uint32_t wifiAttemptAt = 0;
uint32_t wifiRetryAt = 0;

// Statistics ----------------------------------------------------------------
uint32_t txPacketCount = 0;
uint32_t deliveredCount = 0;
uint32_t retryCountTotal = 0;
uint32_t failedCount = 0;

uint32_t rxPacketCount = 0;
uint32_t ackTxCount = 0;
uint32_t ackReceivedCount = 0;
uint32_t duplicateCount = 0;
uint32_t oldPacketCount = 0;
uint32_t authFailureCount = 0;
uint32_t malformedCount = 0;

// ============================================================================
// INTERRUPTS & HELPERS
// ============================================================================

void IRAM_ATTR onLoraTxDone() {
    g_txDone = true;
}

void setStatus(const char* text) {
    snprintf(statusText, sizeof(statusText), "%s", text);
    statusAt = millis();
    statusActive = true;
    screenDirty = true;
}

void fatalError(const char* message) {
    Serial.println(message);
    while (true) delay(1000);
}

bool hexCharToNibble(char c, uint8_t* nibble) {
    if (c >= '0' && c <= '9') { *nibble = c - '0'; return true; }
    if (c >= 'a' && c <= 'f') { *nibble = c - 'a' + 10; return true; }
    if (c >= 'A' && c <= 'F') { *nibble = c - 'A' + 10; return true; }
    return false;
}

void loadAesKeyFromHex() {
    if (strlen(AES_KEY_HEX) != 32) {
        fatalError("[FATAL] AES_KEY_HEX must contain exactly 32 hex characters.");
    }
    for (uint8_t i = 0; i < 16; ++i) {
        uint8_t high = 0, low = 0;
        if (!hexCharToNibble(AES_KEY_HEX[i * 2], &high) ||
            !hexCharToNibble(AES_KEY_HEX[i * 2 + 1], &low)) {
            fatalError("[FATAL] AES_KEY_HEX contains a non-hex character.");
        }
        AES_KEY[i] = uint8_t((high << 4) | low);
    }
}

// ============================================================================
// PERSISTENCE & MORSE ENGINE
// ============================================================================

uint32_t loadSequence(const char* keyName, uint32_t defaultValue) {
    const uint32_t value = preferences.getULong(keyName, defaultValue);
    return value == 0 ? defaultValue : value;
}

void storeSequence(const char* keyName, uint32_t value) {
    if (!preferences.putULong(keyName, value)) {
        fatalError("[FATAL] Failed to persist sequence state.");
    }
}

uint32_t nextTxSequence() {
    const uint32_t current = loadSequence("txseq", 1);
    if (current == UINT32_MAX) fatalError("[FATAL] TX sequence exhausted.");
    storeSequence("txseq", current + 1);
    return current;
}

uint32_t getLastRxSequence() { return preferences.getULong("rxseq", 0); }
void storeLastRxSequence(uint32_t sequence) { storeSequence("rxseq", sequence); }

char decodeMorse(const Marks& marks) {
    if (!marks.length || marks.length > 5) return '?';
    static const char tree[] =
        "??ETIANMSURWDKGOHVF?L?PJBXCYZQ??"
        "54?3???2???????16???????7???8?90";
    uint8_t index = 1;
    for (uint8_t i = 0; i < marks.length; ++i) {
        index = uint8_t(index * 2 + (marks.text[i] == '-'));
    }
    return tree[index];
}

void appendReceivedText(const char* str, uint8_t len) {
    for (uint8_t i = 0; i < len; ++i) {
        if (receivedLength == LOG_CAPACITY) {
            memmove(receivedText, receivedText + 1, LOG_CAPACITY - 1);
            --receivedLength;
        }
        receivedText[receivedLength++] = str[i];
    }
    receivedText[receivedLength] = '\0';
    screenDirty = true;
}

// ============================================================================
// OUTBOX & BATCH TRANSMISSION QUEUE
// ============================================================================

bool flushOutboxToRadio() {
    if (outboxWordLen == 0) return true;
    if (!radioReady) {
        setStatus("RADIO OFFLINE");
        return false;
    }
    if (txCount >= TX_QUEUE_SIZE) {
        setStatus("TX QUEUE FULL");
        return false;
    }

    TxPacket pkt;
    memcpy(pkt.payload, outboxWordBuffer, outboxWordLen);
    pkt.payload[outboxWordLen] = '\0';
    pkt.length = outboxWordLen;
    pkt.sequence = nextTxSequence();

    txQueue[txTail] = pkt;
    txTail = uint8_t((txTail + 1) % TX_QUEUE_SIZE);
    ++txCount;

    outboxWordLen = 0;
    outboxWordBuffer[0] = '\0';
    setStatus("FRAME QUEUED");
    screenDirty = true;
    return true;
}

void appendCharToOutbox(char c) {
    if (outboxWordLen >= MAX_PAYLOAD_LEN) {
        flushOutboxToRadio();
    }
    outboxWordBuffer[outboxWordLen++] = c;
    outboxWordBuffer[outboxWordLen] = '\0';
    lastLetterAt = millis();
    screenDirty = true;

    if (c == ' ' || outboxWordLen >= MAX_PAYLOAD_LEN) {
        flushOutboxToRadio();
    }
}

void finishOutgoingCharacter() {
    if (!outgoingMarks.length) return;
    const char decoded = decodeMorse(outgoingMarks);
    outgoingMarks.clear();
    appendCharToOutbox(decoded);
}

// ============================================================================
// AUDIO SIDE-EFFECTS
// ============================================================================

void queueBeep(uint16_t durationMs) {
    if (beepCount < BEEP_QUEUE_SIZE) {
        beepQueue[beepTail] = durationMs;
        beepTail = uint8_t((beepTail + 1) % BEEP_QUEUE_SIZE);
        ++beepCount;
    }
}

// ============================================================================
// CRYPTO & PACKET BUILDER
// ============================================================================

void writeU16BE(uint8_t* ptr, uint16_t val) { ptr[0] = val >> 8; ptr[1] = val; }
uint16_t readU16BE(const uint8_t* ptr) { return (ptr[0] << 8) | ptr[1]; }
void writeU32BE(uint8_t* ptr, uint32_t val) {
    ptr[0] = val >> 24; ptr[1] = val >> 16; ptr[2] = val >> 8; ptr[3] = val;
}
uint32_t readU32BE(const uint8_t* ptr) {
    return (uint32_t(ptr[0]) << 24) | (uint32_t(ptr[1]) << 16) | (uint32_t(ptr[2]) << 8) | ptr[3];
}

void fillRandomNonce(uint8_t nonce[NONCE_LEN]) {
    for (uint8_t i = 0; i < NONCE_LEN; i += 4) {
        uint32_t r = esp_random();
        memcpy(nonce + i, &r, 4);
    }
}

bool buildPacket(PacketType type, uint32_t sequence, const uint8_t* plaintext, uint8_t plaintextLen, uint8_t* out, size_t* outLen) {
    if (plaintextLen > MAX_PAYLOAD_LEN) return false;
    memset(out, 0, MAX_PACKET_LEN);

    out[0] = PACKET_MAGIC;
    out[1] = PROTOCOL_VERSION;
    out[2] = static_cast<uint8_t>(type);
    writeU16BE(out + 3, DEVICE_ID);
    writeU16BE(out + 5, PEER_DEVICE_ID);
    writeU32BE(out + 7, sequence);
    fillRandomNonce(out + 11);
    out[23] = plaintextLen;

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, AES_KEY, 128) != 0) {
        mbedtls_gcm_free(&gcm);
        return false;
    }

    uint8_t* ciphertext = out + HEADER_LEN;
    uint8_t* tag = ciphertext + plaintextLen;
    int rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, plaintextLen, out + 11, NONCE_LEN, out, HEADER_LEN, plaintext, ciphertext, TAG_LEN, tag);
    mbedtls_gcm_free(&gcm);
    if (rc != 0) return false;

    *outLen = HEADER_LEN + plaintextLen + TAG_LEN;
    return true;
}

bool decryptPacket(const uint8_t* packet, size_t packetLen, PacketType* type, uint16_t* source, uint16_t* destination, uint32_t* sequence, uint8_t* plaintext, uint8_t* plaintextLen) {
    if (packetLen < HEADER_LEN + TAG_LEN) { ++malformedCount; return false; }
    if (packet[0] != PACKET_MAGIC || packet[1] != PROTOCOL_VERSION) { ++malformedCount; return false; }

    *type = static_cast<PacketType>(packet[2]);
    *source = readU16BE(packet + 3);
    *destination = readU16BE(packet + 5);
    *sequence = readU32BE(packet + 7);

    if (*destination != DEVICE_ID) { ++malformedCount; return false; }
    const uint8_t payloadLen = packet[23];
    if (payloadLen > MAX_PAYLOAD_LEN || packetLen != HEADER_LEN + payloadLen + TAG_LEN) { ++malformedCount; return false; }

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, AES_KEY, 128) != 0) {
        mbedtls_gcm_free(&gcm);
        ++authFailureCount;
        return false;
    }

    const uint8_t* ciphertext = packet + HEADER_LEN;
    const uint8_t* tag = packet + HEADER_LEN + payloadLen;
    int rc = mbedtls_gcm_auth_decrypt(&gcm, payloadLen, packet + 11, NONCE_LEN, packet, HEADER_LEN, tag, TAG_LEN, ciphertext, plaintext);
    mbedtls_gcm_free(&gcm);

    if (rc != 0) { ++authFailureCount; return false; }
    *plaintextLen = payloadLen;
    return true;
}

// ============================================================================
// RADIO ENGINE & ARQ STATE MACHINE
// ============================================================================

bool transmitFrame(const uint8_t* frame, size_t length, bool expectAck, uint32_t now) {
    if (!radioReady || length > MAX_PACKET_LEN) return false;
    g_txDone = false;
    if (!LoRa.beginPacket()) { g_txDone = true; return false; }
    if (LoRa.write(frame, length) != length) { LoRa.idle(); g_txDone = true; return false; }
    if (!LoRa.endPacket(true)) { LoRa.idle(); g_txDone = true; return false; }

    digitalWrite(TX_LED_PIN, HIGH);
    txPulse = true;
    txPulseAt = now;
    txStartedAt = now;

    if (expectAck) txState = TxState::Transmitting;
    else { ackTxBusy = true; ackTxStartedAt = now; }
    return true;
}

bool sendAckNow(uint32_t sequence, uint32_t now) {
    uint8_t frame[MAX_PACKET_LEN] = {};
    size_t frameLen = 0;
    if (!buildPacket(PacketType::Ack, sequence, nullptr, 0, frame, &frameLen)) return false;
    if (!transmitFrame(frame, frameLen, false, now)) return false;
    ackPending = false;
    ++ackTxCount;
    return true;
}

bool startDataTransmission(uint32_t now) {
    if (!radioReady || txState != TxState::Idle || ackTxBusy || txCount == 0) return false;

    inFlightPacket = txQueue[txHead];
    size_t frameLen = 0;
    if (!buildPacket(PacketType::Data, inFlightPacket.sequence, (const uint8_t*)inFlightPacket.payload, inFlightPacket.length, inFlightFrame, &frameLen)) {
        setStatus("BUILD ERR");
        return false;
    }

    if (!transmitFrame(inFlightFrame, frameLen, true, now)) {
        setStatus("TX ERR");
        return false;
    }

    inFlightFrameLen = frameLen;
    txHead = uint8_t((txHead + 1) % TX_QUEUE_SIZE);
    --txCount;
    retryCount = 0;
    ++txPacketCount;
    setStatus("SENDING...");
    screenDirty = true;
    return true;
}

bool retryInFlight(uint32_t now) {
    if (!radioReady || txState != TxState::WaitingAck || inFlightFrameLen == 0) return false;
    if (!transmitFrame(inFlightFrame, inFlightFrameLen, true, now)) return false;
    ++retryCount;
    ++retryCountTotal;
    ++txPacketCount;

    char msg[32];
    snprintf(msg, sizeof(msg), "RETRY %u/%u", retryCount, MAX_RETRIES);
    setStatus(msg);
    screenDirty = true;
    return true;
}

void completeDelivered() {
    txState = TxState::Idle;
    retryCount = 0;
    ++deliveredCount;
    setStatus("DELIVERED ✓");
    screenDirty = true;
}

void failInFlight() {
    txState = TxState::Idle;
    retryCount = 0;
    ++failedCount;
    setStatus("FAILED !");
    screenDirty = true;
}

void handleIncomingPacket(const uint8_t* packet, size_t packetLen, uint32_t now) {
    PacketType type;
    uint16_t source = 0, destination = 0;
    uint32_t sequence = 0;
    uint8_t payload[MAX_PAYLOAD_LEN + 1] = {};
    uint8_t payloadLen = 0;

    if (!decryptPacket(packet, packetLen, &type, &source, &destination, &sequence, payload, &payloadLen)) {
        setStatus("BAD PACKET");
        return;
    }
    if (source != PEER_DEVICE_ID) return;

    if (type == PacketType::Ack) {
        if (txState == TxState::WaitingAck && sequence == inFlightPacket.sequence) {
            ++ackReceivedCount;
            completeDelivered();
        }
        return;
    }

    if (type == PacketType::Data) {
        ++rxPacketCount;
        const uint32_t lastSeq = getLastRxSequence();
        if (sequence == lastSeq) {
            ++duplicateCount;
            ackPending = true;
            pendingAckSeq = sequence;
            return;
        }
        if (sequence < lastSeq) {
            ++oldPacketCount;
            return;
        }

        storeLastRxSequence(sequence);
        ackPending = true;
        pendingAckSeq = sequence;

        payload[payloadLen] = '\0';
        appendReceivedText((const char*)payload, payloadLen);
        queueBeep(80); // Play notification tone on packet receipt

        rxPulse = true;
        rxPulseAt = now;
        digitalWrite(RX_LED_PIN, HIGH);
        setStatus("RX FRAME");
    }
}

void serviceRadio(uint32_t now) {
    if (!radioReady || otaActive) return;

    if (ackTxBusy) {
        if (!g_txDone) {
            if (uint32_t(now - ackTxStartedAt) >= TX_TIMEOUT_MS) {
                ackTxBusy = false; g_txDone = true; LoRa.idle();
            }
            return;
        }
        ackTxBusy = false;
    }

    if (txState == TxState::Transmitting) {
        if (!g_txDone) {
            if (uint32_t(now - txStartedAt) >= TX_TIMEOUT_MS) {
                txState = TxState::Idle; g_txDone = true; LoRa.idle(); setStatus("TX TIMEOUT");
            }
            return;
        }
        txState = TxState::WaitingAck;
        ackWaitStartedAt = now;
        setStatus("WAIT ACK");
        screenDirty = true;
    }

    const int packetSize = LoRa.parsePacket();
    if (packetSize > 0) {
        lastRssi = LoRa.packetRssi();
        lastSnr = LoRa.packetSnr();
        hasRssi = hasSnr = true;

        uint8_t packet[MAX_PACKET_LEN] = {};
        size_t received = 0;
        while (LoRa.available() && received < sizeof(packet)) {
            packet[received++] = uint8_t(LoRa.read());
        }
        if (received == static_cast<size_t>(packetSize)) {
            handleIncomingPacket(packet, received, now);
        }
    }

    if (ackPending && !ackTxBusy) {
        if (sendAckNow(pendingAckSeq, now)) return;
    }

    if (txState == TxState::WaitingAck) {
        if (uint32_t(now - ackWaitStartedAt) >= ACK_TIMEOUT_MS) {
            if (retryCount < MAX_RETRIES) retryInFlight(now);
            else failInFlight();
        }
        return;
    }

    startDataTransmission(now);
}

// ============================================================================
// INPUT PROCESSING
// ============================================================================

void resetInputs(uint32_t now) {
    key.begin(now);
    control.begin(now);
    ignoreKeyUntilRelease = key.down;
    controlHandled = control.down;
}

void serviceInputs(uint32_t now) {
    key.update(now);
    control.update(now);

    if (key.pressed && !ignoreKeyUntilRelease) {
        if (outgoingMarks.length && uint32_t(key.edgeAt - lastMarkAt) >= CHARACTER_PAUSE_MS) {
            finishOutgoingCharacter();
        }
        keyStartedAt = key.edgeAt;
        screenDirty = true;
    }

    if (key.released) {
        if (ignoreKeyUntilRelease) {
            ignoreKeyUntilRelease = false;
        } else {
            const uint32_t duration = key.edgeAt - keyStartedAt;
            const char mark = (duration < DOT_DASH_SPLIT_MS) ? '.' : '-';
            outgoingMarks.append(mark);
            lastMarkAt = key.edgeAt;
            setStatus(mark == '.' ? "DOT" : "DASH");
        }
        screenDirty = true;
    }

    if (control.pressed) {
        controlStartedAt = control.edgeAt;
        controlHandled = false;
        screenDirty = true;
    }

    if (control.down && !controlHandled && uint32_t(now - controlStartedAt) >= CONTROL_HOLD_MS) {
        controlHandled = true;
        finishOutgoingCharacter();
        appendCharToOutbox(' ');
        setStatus("SPACE");
    }

    if (control.released) {
        if (!controlHandled) {
            // Short tap on control flushes buffer immediately
            finishOutgoingCharacter();
            flushOutboxToRadio();
        }
        screenDirty = true;
    }

    // Process character pause completion
    if (!key.down && !key.raw && outgoingMarks.length && uint32_t(now - lastMarkAt) >= CHARACTER_PAUSE_MS) {
        finishOutgoingCharacter();
    }

    // Process auto word flush on idle
    if (outboxWordLen > 0 && uint32_t(now - lastLetterAt) >= AUTO_FLUSH_IDLE_MS) {
        flushOutboxToRadio();
    }
}

// ============================================================================
// HARDWARE OUTPUTS (AUDIO & LEDS)
// ============================================================================

void stopOutputs() {
    txPulse = rxPulse = beepOn = beepGap = buzzerHigh = false;
    beepCount = beepHead = beepTail = 0;
    digitalWrite(TX_LED_PIN, LOW);
    digitalWrite(RX_LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
}

void serviceOutputs(uint32_t now) {
    if (txPulse && uint32_t(now - txPulseAt) >= 120 && txState != TxState::Transmitting && !ackTxBusy) {
        txPulse = false; digitalWrite(TX_LED_PIN, LOW); screenDirty = true;
    }
    if (rxPulse && uint32_t(now - rxPulseAt) >= 150) {
        rxPulse = false; digitalWrite(RX_LED_PIN, LOW); screenDirty = true;
    }

    if (beepOn && uint32_t(now - beepAt) >= beepDuration) {
        beepOn = false; beepGap = true; beepAt = now;
    }
    if (beepGap && uint32_t(now - beepAt) >= 40) {
        beepGap = false;
    }
    if (!beepOn && !beepGap && beepCount) {
        beepDuration = beepQueue[beepHead];
        beepHead = uint8_t((beepHead + 1) % BEEP_QUEUE_SIZE);
        --beepCount;
        beepAt = now;
        beepOn = true;
    }

    const bool sound = !otaActive && (beepOn || (KEY_SIDETONE && key.down && !ignoreKeyUntilRelease));
    if (sound != buzzerHigh) {
        buzzerHigh = sound;
        digitalWrite(BUZZER_PIN, sound ? HIGH : LOW);
    }
}

void serviceTimers(uint32_t now) {
    if (statusActive && uint32_t(now - statusAt) >= STATUS_HOLD_MS) {
        statusActive = false; screenDirty = true;
    }
    if (uint32_t(now - footerAt) >= FOOTER_PAGE_MS) {
        footerAt = now; footerPage = uint8_t((footerPage + 1) % 3); screenDirty = true;
    }
}

// ============================================================================
// UI & DISPLAY RENDERING (SH1106 FULL FRAME REFRESH)
// ============================================================================

void centered(const char* text, int x, int baseline, int width) {
    const int textWidth = display.getStrWidth(text);
    display.drawStr(x + (width > textWidth ? (width - textWidth) / 2 : 0), baseline, text);
}

void drawUi(uint32_t now) {
    display.clearBuffer();
    display.setDrawColor(1);
    display.setFontMode(1);

    // Header bar
    display.setFont(u8g2_font_4x6_tf);
    display.drawStr(2, 7, "LORA-CW");
    char devBuf[8]; snprintf(devBuf, sizeof(devBuf), "D%02X", DEVICE_ID & 0xFF);
    display.drawStr(38, 7, devBuf);

    if (hasRssi) {
        char rssi[8]; snprintf(rssi, sizeof(rssi), "%d", lastRssi);
        display.drawStr(72, 7, rssi);
    } else {
        display.drawStr(58, 7, "RX --");
    }
    display.drawHLine(0, 9, SCREEN_W);

    // Outbox & Input Cards
    display.drawRFrame(2, 12, 124, 24, 2);
    display.setFont(u8g2_font_4x6_tf);
    display.drawStr(6, 19, "OUTBOX:");

    display.setFont(u8g2_font_6x10_tf);
    if (outboxWordLen > 0) {
        display.drawStr(42, 21, outboxWordBuffer);
    } else {
        display.drawStr(42, 21, "<KEYING>");
    }

    // Active keying mark line
    if (outgoingMarks.length) {
        int mx = 6;
        for (uint8_t i = 0; i < outgoingMarks.length; ++i) {
            uint8_t w = (outgoingMarks.text[i] == '-') ? 5 : 2;
            display.drawBox(mx, 29, w, 2);
            mx += w + 2;
        }
    }

    // Message Log Display
    display.drawHLine(0, 38, SCREEN_W);
    display.setFont(u8g2_font_5x7_tf);
    if (!receivedLength) {
        centered("NO RX MESSAGES", 2, 47, 124);
    } else {
        for (uint8_t row = 0; row < 2; ++row) {
            const uint8_t offset = row * LOG_COLS;
            if (offset >= receivedLength) break;
            uint8_t count = receivedLength - offset;
            if (count > LOG_COLS) count = LOG_COLS;
            char line[LOG_COLS + 1] = {};
            memcpy(line, receivedText + offset, count);
            display.drawStr(2, 47 + row * 7, line);
        }
    }

    // Footer Bar
    display.drawHLine(0, 56, SCREEN_W);
    display.setFont(u8g2_font_4x6_tf);
    char ftr[64] = {};
    if (statusActive) {
        snprintf(ftr, sizeof(ftr), "%s", statusText);
    } else if (txState == TxState::WaitingAck) {
        snprintf(ftr, sizeof(ftr), "WAIT ACK (%u/%u)", retryCount, MAX_RETRIES);
    } else {
        snprintf(ftr, sizeof(ftr), "TX:%lu ACK:%lu ERR:%lu", (unsigned long)txPacketCount, (unsigned long)deliveredCount, (unsigned long)failedCount);
    }
    centered(ftr, 2, 63, 124);
}

void serviceDisplay(uint32_t now) {
    if (otaActive) return;

    const bool animated = key.down || outgoingMarks.length || outboxWordLen > 0 || txState != TxState::Idle;
    if ((!screenDirty && !animated) || uint32_t(now - frameAt) < UI_FRAME_MS) return;

    drawUi(now);
    display.sendBuffer(); // SH1106 full buffer send
    screenDirty = false;
    frameAt = now;
}

// ============================================================================
// WIFI & OTA ENGINE
// ============================================================================

void maintainWiFi(uint32_t now) {
    if (!WIFI_SSID[0] || uint32_t(now - wifiPollAt) < 250) return;
    wifiPollAt = now;

    if (WiFi.status() == WL_CONNECTED) {
        if (!wifiConnected) {
            wifiConnected = true;
            wifiAttempting = false;
            snprintf(ipText, sizeof(ipText), "%s", WiFi.localIP().toString().c_str());
            if (ENABLE_OTA && OTA_HOSTNAME[0]) { ArduinoOTA.begin(); otaStarted = true; }
            screenDirty = true;
        }
    } else if (wifiConnected) {
        wifiConnected = false;
        wifiAttempting = false;
        wifiRetryAt = now;
        screenDirty = true;
    }
}

// ============================================================================
// HARDWARE INITIALIZATION
// ============================================================================

void configureLoRa() {
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
    radioReady = (LoRa.begin(LORA_FREQUENCY) != 0);
    if (!radioReady) return;

    LoRa.onTxDone(onLoraTxDone);
    LoRa.setSignalBandwidth(LORA_BANDWIDTH);
    LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
    LoRa.setCodingRate4(LORA_CODING_RATE);
    LoRa.setSyncWord(LORA_SYNC_WORD);
    LoRa.setPreambleLength(LORA_PREAMBLE_LENGTH);
    LoRa.setTxPower(LORA_TX_POWER);
    if (LORA_ENABLE_CRC) LoRa.enableCrc();
    LoRa.parsePacket();
}

void setup() {
    Serial.begin(115200);
    delay(50);
    Serial.println("\n=== LORA-CW BATCHING ENGINE ===");

    loadAesKeyFromHex();

    if (!preferences.begin("lora-cw", false)) {
        fatalError("[FATAL] Failed to open NVS storage.");
    }

    pinMode(KEY_PIN, INPUT_PULLUP);
    pinMode(CONTROL_PIN, INPUT_PULLUP);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(TX_LED_PIN, OUTPUT);
    pinMode(RX_LED_PIN, OUTPUT);

    stopOutputs();
    resetInputs(millis());

    // SH1106 OLED Initialization Sequence
    Wire.begin(OLED_SDA, OLED_SCL);
    display.begin();                   // Must be called before setBusClock
    display.setBusClock(OLED_I2C_HZ);
    display.setContrast(100);
    display.setFontMode(1);
    display.clearBuffer();

    display.setFont(u8g2_font_logisoso16_tf);
    centered("LORA-CW", 0, 28, 128);
    display.setFont(u8g2_font_5x7_tf);
    centered("STARTING RADIO", 0, 47, 128);
    display.sendBuffer();

    configureLoRa();

    if (WIFI_SSID[0]) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        wifiAttempting = true;
    }

    setStatus(radioReady ? "READY" : "RADIO FAIL");
    frameAt = millis() - UI_FRAME_MS;
    footerAt = millis();
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    const uint32_t now = millis();

    if (!otaActive) {
        serviceInputs(now);
        serviceRadio(now);
        serviceOutputs(now);
        serviceTimers(now);
        maintainWiFi(now);
    }

    if (wifiConnected && otaStarted && !otaActive) {
        ArduinoOTA.handle();
    }

    serviceDisplay(now);
    yield();
}
