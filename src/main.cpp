#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#define private public
#include <LoRa.h>
#undef private
#include <U8g2lib.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "mbedtls/aes.h"
#include "esp_system.h"

// Configuration -------------------------------------------------------------
const char* WIFI_SSID = "";
const char* WIFI_PASS = "";
const char* OTA_HOSTNAME = "";
const char* OTA_PASSWORD = "";  // Optional; empty preserves the original setup.

constexpr long LORA_FREQUENCY = 433000000L;
constexpr uint32_t OLED_I2C_HZ = 400000;
constexpr bool KEY_SIDETONE = true;

constexpr uint32_t DOT_DASH_SPLIT_MS = 250;
constexpr uint32_t CHARACTER_PAUSE_MS = 850;
constexpr uint32_t DEBOUNCE_MS = 8;
constexpr uint32_t CONTROL_HOLD_MS = 700;
constexpr uint32_t RX_STALE_MS = 10000;
constexpr uint32_t TX_TIMEOUT_MS = 4000;  // Increase if using very slow RF settings.
constexpr uint32_t TX_FLASH_MS = 120;
constexpr uint32_t RX_FLASH_MS = 150;
constexpr uint16_t BEEP_DOT_MS = 55;
constexpr uint16_t BEEP_DASH_MS = 170;
constexpr uint16_t BEEP_GAP_MS = 40;
constexpr uint32_t UI_FRAME_MS = 40;
constexpr uint32_t STATUS_HOLD_MS = 1500;
constexpr uint32_t FOOTER_PAGE_MS = 5000;
constexpr uint32_t WIFI_POLL_MS = 250;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 10000;

constexpr int LORA_SCK = 18, LORA_MISO = 19, LORA_MOSI = 23;
constexpr int LORA_CS = 16, LORA_RST = 26, LORA_DIO0 = 25;
constexpr int OLED_SDA = 21, OLED_SCL = 22;
constexpr int BUZZER_PIN = 33, TX_LED_PIN = 32, RX_LED_PIN = 13;
constexpr int KEY_PIN = 27, CONTROL_PIN = 14;

constexpr uint8_t SCREEN_W = 128, SCREEN_H = 64;
constexpr uint8_t MAX_MARKS = 6;
constexpr uint8_t LOG_COLS = 24, LOG_CAPACITY = 2 * LOG_COLS;
constexpr uint8_t TX_QUEUE_SIZE = 32, BEEP_QUEUE_SIZE = 16;
constexpr uint8_t TILES_X = SCREEN_W / 8, TILES_Y = SCREEN_H / 8;
constexpr uint16_t TILE_COUNT = TILES_X * TILES_Y;
constexpr uint8_t TILES_PER_TRANSFER = 4;
constexpr size_t FRAME_BYTES = SCREEN_W * SCREEN_H / 8;

// --- Encryption --------------------------------------------------------
// Pre-shared 128-bit key. MUST be identical on both ends of the radio link.
constexpr uint8_t AES_KEY[16] = {

};

constexpr uint8_t NONCE_LEN = 8;
constexpr uint8_t ENC_PACKET_LEN = NONCE_LEN + 2;  // nonce + ciphertext byte + tag byte

U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
    U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);

// Small, fixed-size state objects --------------------------------------------
struct Marks {
    char text[MAX_MARKS + 1] = "";
    uint8_t length = 0;
    bool overflow = false;

    void clear() { length = 0; text[0] = '\0'; overflow = false; }
    void append(char mark) {
        if (length == MAX_MARKS) { overflow = true; return; }
        text[length++] = mark;
        text[length] = '\0';
    }
};

struct Button {
    int pin;
    bool raw = false, down = false;
    bool pressed = false, released = false;
    uint32_t rawAt = 0, edgeAt = 0;

    explicit Button(int gpio) : pin(gpio) {}
    void begin(uint32_t now) {
        raw = down = (digitalRead(pin) == LOW);
        rawAt = edgeAt = now;
        pressed = released = false;
    }
    void update(uint32_t now) {
        pressed = released = false;
        const bool sample = digitalRead(pin) == LOW;
        if (sample != raw) { raw = sample; rawAt = now; }
        if (down != raw && uint32_t(now - rawAt) >= DEBOUNCE_MS) {
            down = raw;
            edgeAt = rawAt;  // Measure the observed edge, not the debounce delay.
            pressed = down;
            released = !down;
        }
    }
};

struct TxItem { char token; char decoded; };

Marks outgoing, incoming;
Button key(KEY_PIN), control(CONTROL_PIN);
TxItem txQueue[TX_QUEUE_SIZE] = {}, inFlight = {};
uint8_t txHead = 0, txTail = 0, txCount = 0;
uint16_t beepQueue[BEEP_QUEUE_SIZE] = {};
uint8_t beepHead = 0, beepTail = 0, beepCount = 0;
char receivedText[LOG_CAPACITY + 1] = "";
uint8_t receivedLength = 0;
char lastTxChar = '\0', lastRxChar = '\0';
char statusText[32] = "";
char ipText[16] = "";
bool radioReady = false, wifiConnected = false, wifiAttempting = false;
bool otaStarted = false, otaActive = false;
uint8_t otaPercent = 0;
bool txBusy = false, txPulse = false, rxPulse = false;
bool beepOn = false, beepGap = false, buzzerHigh = false;
bool ignoreKeyUntilRelease = false, controlHandled = false;
bool rxDiscarding = false, hasRssi = false;
int lastRssi = 0;
bool statusActive = false, footerShowsIp = false;
bool screenDirty = true;
uint8_t previousFrame[FRAME_BYTES] = {};
uint16_t nextTile = TILE_COUNT;
uint32_t keyStartedAt = 0, controlStartedAt = 0, lastMarkAt = 0;
uint32_t lastRxMarkAt = 0, txStartedAt = 0, txPulseAt = 0, rxPulseAt = 0;
uint32_t beepAt = 0, beepDuration = 0;
uint32_t statusAt = 0, footerAt = 0, frameAt = 0;
uint32_t wifiPollAt = 0, wifiAttemptAt = 0, wifiRetryAt = 0;

// Explicit prototypes keep Arduino's sketch preprocessor away from custom types.
char decodeMorse(const Marks& marks);
void drawCard(int x, const char* label, const Marks& marks, char last, bool lit);
void serviceDisplay(uint32_t now);
void resetInputs(uint32_t now);
void stopOutputs();

// Text and protocol helpers --------------------------------------------------
char decodeMorse(const Marks& marks) {
    if (!marks.length || marks.overflow || marks.length > 5) return '?';
    // Binary Morse tree: start at 1; dot -> 2*i, dash -> 2*i+1.
    static const char tree[] =
        "??ETIANMSURWDKGOHVF?L?PJBXCYZQ??"
        "54?3???2???????16???????7???8?90";
    static_assert(sizeof(tree) == 65, "Morse tree must have 64 entries");
    uint8_t index = 1;
    for (uint8_t i = 0; i < marks.length; ++i) {
        index = uint8_t(index * 2 + (marks.text[i] == '-'));
    }
    return tree[index];
}

void setStatus(const char* text) {
    snprintf(statusText, sizeof(statusText), "%s", text);
    statusAt = millis();
    statusActive = true;
    screenDirty = true;
}

void letterStatus(const char* prefix, char letter) {
    char message[32];
    snprintf(message, sizeof(message), "%s %c", prefix, letter);
    setStatus(message);
}

void appendReceivedLetter(char letter) {
    if (receivedLength == LOG_CAPACITY) {
        memmove(receivedText, receivedText + 1, LOG_CAPACITY - 1);
        --receivedLength;
    }
    receivedText[receivedLength++] = letter;
    receivedText[receivedLength] = '\0';
    screenDirty = true;
}

bool queueToken(char token, char decoded, bool reserveDelimiter) {
    if (!radioReady) { setStatus("RADIO OFFLINE"); return false; }
    // A mark always leaves room for its terminating slash.
    const uint8_t limit = TX_QUEUE_SIZE - (reserveDelimiter ? 1 : 0);
    if (txCount >= limit) { setStatus("TX QUEUE FULL"); return false; }
    txQueue[txTail] = {token, decoded};
    txTail = uint8_t((txTail + 1) % TX_QUEUE_SIZE);
    ++txCount;
    screenDirty = true;
    return true;
}

bool finishOutgoingCharacter() {
    if (!outgoing.length) return true;
    const char decoded = decodeMorse(outgoing);
    if (!queueToken('/', decoded, false)) return false;
    outgoing.clear();
    letterStatus("QUEUED", decoded);
    return true;
}

void sendWordSpace() {
    if (!radioReady) { setStatus("RADIO OFFLINE"); return; }
    const uint8_t needed = outgoing.length ? 2 : 1;
    if (TX_QUEUE_SIZE - txCount < needed) { setStatus("TX QUEUE FULL"); return; }
    if (!finishOutgoingCharacter()) return;
    if (queueToken(' ', ' ', false)) setStatus("SPACE QUEUED");
}

void clearBuffers() {
    // Close the remote letter before discarding local TX state.
    if (!finishOutgoingCharacter()) return;
    rxDiscarding = rxDiscarding || incoming.length != 0;
    incoming.clear();
    receivedLength = 0;
    receivedText[0] = '\0';
    lastTxChar = lastRxChar = '\0';
    hasRssi = false;
    if (key.down || key.raw) ignoreKeyUntilRelease = true;
    setStatus("BUFFERS CLEARED");
}

void queueBeep(char token) {
    if (beepCount == BEEP_QUEUE_SIZE) return;  // Audio never blocks RF processing.
    beepQueue[beepTail] = token == '-' ? BEEP_DASH_MS : BEEP_DOT_MS;
    beepTail = uint8_t((beepTail + 1) % BEEP_QUEUE_SIZE);
    ++beepCount;
}

void finishReceivedCharacter() {
    if (!rxDiscarding && incoming.length) {
        lastRxChar = decodeMorse(incoming);
        appendReceivedLetter(lastRxChar);
        letterStatus("RX", lastRxChar);
    }
    incoming.clear();
    rxDiscarding = false;
}

void handleReceivedToken(char token, uint32_t now) {
    if (token != '.' && token != '-' && token != '/' && token != ' ') return;
    rxPulse = true;
    rxPulseAt = now;
    digitalWrite(RX_LED_PIN, HIGH);
    if (token == '.' || token == '-') {
        lastRxMarkAt = now;
        if (!rxDiscarding) incoming.append(token);
        queueBeep(token);
        setStatus(token == '.' ? "RX DOT" : "RX DASH");
    } else {
        // Space is also a boundary if the preceding slash was lost.
        finishReceivedCharacter();
        if (token == ' ') {
            lastRxChar = ' ';
            appendReceivedLetter(' ');
            setStatus("RX SPACE");
        }
    }
    screenDirty = true;
}

void radioFailure(const char* message) {
    LoRa.idle();
    radioReady = false;
    txBusy = false;
    txCount = txHead = txTail = 0;
    outgoing.clear();
    txPulse = false;
    digitalWrite(TX_LED_PIN, LOW);
    setStatus(message);
    Serial.println(message);
}

// --- Encryption --------------------------------------------------------
void deriveKeystream(const uint8_t nonce[NONCE_LEN], uint8_t keystream[16]) {
    uint8_t block[16] = {0};
    memcpy(block, nonce, NONCE_LEN);  // Remaining bytes are zero padding.

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, AES_KEY, 128);
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, block, keystream);
    mbedtls_aes_free(&aes);
}

void encryptToken(uint8_t token, uint8_t out[ENC_PACKET_LEN]) {
    uint8_t nonce[NONCE_LEN];
    const uint32_t r1 = esp_random(), r2 = esp_random();  // Hardware TRNG.
    memcpy(nonce, &r1, 4);
    memcpy(nonce + 4, &r2, 4);

    uint8_t keystream[16];
    deriveKeystream(nonce, keystream);

    memcpy(out, nonce, NONCE_LEN);
    out[NONCE_LEN] = token ^ keystream[0];
    out[NONCE_LEN + 1] = keystream[1];  // Authentication tag.
}

bool decryptToken(const uint8_t in[ENC_PACKET_LEN], uint8_t* token) {
    uint8_t keystream[16];
    deriveKeystream(in, keystream);  // First NONCE_LEN bytes of `in` are the nonce.
    if (in[NONCE_LEN + 1] != keystream[1]) return false;  // Bad tag: drop the packet.
    *token = in[NONCE_LEN] ^ keystream[0];
    return true;
}

void serviceRadio(uint32_t now) {
    if (!radioReady || otaActive) return;
    if (txBusy) {
        if (LoRa.isTransmitting()) {
            if (uint32_t(now - txStartedAt) >= TX_TIMEOUT_MS) radioFailure("TX TIMEOUT");
            return;
        }
        txBusy = false;
        if (inFlight.token == '/' && inFlight.decoded) {
            lastTxChar = inFlight.decoded;
            letterStatus("SENT", lastTxChar);
        } else if (inFlight.token == ' ') {
            lastTxChar = ' ';
            setStatus("SPACE SENT");
        }
        screenDirty = true;
    }

    // parsePacket changes radio mode: NEVER call it while async TX is running.
    const int packetSize = LoRa.parsePacket();
    if (packetSize > 0) {
        lastRssi = LoRa.packetRssi();
        hasRssi = true;
        if (packetSize == ENC_PACKET_LEN) {
            uint8_t packet[ENC_PACKET_LEN];
            uint8_t received = 0;
            while (LoRa.available() && received < ENC_PACKET_LEN) {
                const int value = LoRa.read();
                if (value >= 0) packet[received++] = uint8_t(value);
            }
            uint8_t token = 0;
            if (received == ENC_PACKET_LEN && decryptToken(packet, &token)) {
                handleReceivedToken(char(token), now);
            } else {
                setStatus("BAD/UNAUTH PACKET");
            }
        } else {
            while (LoRa.available()) LoRa.read();  // Discard unexpected-size packet.
            setStatus("BAD PACKET SIZE");
        }
        screenDirty = true;
        if (!txCount) LoRa.parsePacket();  // Re-arm single RX after consuming FIFO.
    }
    if (!txCount) return;
    if (!LoRa.beginPacket()) return;
    const TxItem item = txQueue[txHead];
    uint8_t packet[ENC_PACKET_LEN];
    encryptToken(uint8_t(item.token), packet);
    if (LoRa.write(packet, ENC_PACKET_LEN) != ENC_PACKET_LEN || !LoRa.endPacket(true)) {
        radioFailure("TX START FAILED");
        return;
    }
    inFlight = item;
    txHead = uint8_t((txHead + 1) % TX_QUEUE_SIZE);
    --txCount;
    txBusy = txPulse = true;
    txStartedAt = txPulseAt = millis();
    digitalWrite(TX_LED_PIN, HIGH);
    screenDirty = true;
}

// Inputs and output timers ---------------------------------------------------
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
        // Catch a new press that starts just after the letter-gap boundary.
        if (outgoing.length && uint32_t(key.edgeAt - lastMarkAt) >= CHARACTER_PAUSE_MS)
            finishOutgoingCharacter();
        keyStartedAt = key.edgeAt;
        screenDirty = true;
    }
    if (key.released) {
        if (ignoreKeyUntilRelease) {
            ignoreKeyUntilRelease = false;
        } else {
            const uint32_t duration = key.edgeAt - keyStartedAt;
            const char mark = duration < DOT_DASH_SPLIT_MS ? '.' : '-';
            if (queueToken(mark, '\0', true)) {
                outgoing.append(mark);
                lastMarkAt = key.edgeAt;
                setStatus(outgoing.overflow ? "TOO MANY MARKS" :
                          (mark == '.' ? "TX DOT" : "TX DASH"));
            }
        }
        screenDirty = true;
    }

    if (control.pressed) {
        controlStartedAt = control.edgeAt;
        controlHandled = false;
        screenDirty = true;
    }
    if (control.down && !controlHandled &&
        uint32_t(now - controlStartedAt) >= CONTROL_HOLD_MS) {
        controlHandled = true;
        if (key.down || key.raw) setStatus("RELEASE KEY FIRST");
        else sendWordSpace();
    }
    if (control.released) {
        if (!controlHandled) clearBuffers();
        screenDirty = true;
    }
    // raw prevents finalizing during a press that is still being debounced.
    if (!key.down && !key.raw && outgoing.length &&
        uint32_t(now - lastMarkAt) >= CHARACTER_PAUSE_MS) finishOutgoingCharacter();
}

void stopOutputs() {
    txPulse = rxPulse = beepOn = beepGap = buzzerHigh = false;
    beepCount = beepHead = beepTail = 0;
    digitalWrite(TX_LED_PIN, LOW);
    digitalWrite(RX_LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
}

void serviceOutputs(uint32_t now) {
    if (txPulse && uint32_t(now - txPulseAt) >= TX_FLASH_MS && !txBusy) {
        txPulse = false;
        digitalWrite(TX_LED_PIN, LOW);
        screenDirty = true;
    }
    if (rxPulse && uint32_t(now - rxPulseAt) >= RX_FLASH_MS) {
        rxPulse = false;
        digitalWrite(RX_LED_PIN, LOW);
        screenDirty = true;
    }
    if (beepOn && uint32_t(now - beepAt) >= beepDuration) {
        beepOn = false;
        beepGap = true;
        beepAt = now;
    }
    if (beepGap && uint32_t(now - beepAt) >= BEEP_GAP_MS) beepGap = false;
    if (!beepOn && !beepGap && beepCount) {
        beepDuration = beepQueue[beepHead];
        beepHead = uint8_t((beepHead + 1) % BEEP_QUEUE_SIZE);
        --beepCount;
        beepAt = now;
        beepOn = true;
    }
    const bool sound = !otaActive &&
        (beepOn || (KEY_SIDETONE && key.down && !ignoreKeyUntilRelease));
    if (sound != buzzerHigh) {
        buzzerHigh = sound;
        digitalWrite(BUZZER_PIN, sound ? HIGH : LOW);
    }
}

void serviceTimers(uint32_t now) {
    if (statusActive && uint32_t(now - statusAt) >= STATUS_HOLD_MS) {
        statusActive = false;
        screenDirty = true;  // Explicit expiry redraw, even when otherwise idle.
    }
    if (uint32_t(now - footerAt) >= FOOTER_PAGE_MS) {
        footerAt = now;
        footerShowsIp = !footerShowsIp;
        screenDirty = true;
    }
    if ((incoming.length || rxDiscarding) &&
        uint32_t(now - lastRxMarkAt) >= RX_STALE_MS) {
        incoming.clear();
        rxDiscarding = false;
        setStatus("RX GAP / LOST END");
    }
}

// 128 x 64 UI ---------------------------------------------------------------
void centered(const char* text, int x, int baseline, int width) {
    const int textWidth = display.getStrWidth(text);
    display.drawStr(x + (width > textWidth ? (width - textWidth) / 2 : 0), baseline, text);
}

void drawBars(int x, int bottom, uint8_t count) {
    for (uint8_t i = 0; i < 4; ++i) {
        const uint8_t height = uint8_t(1 + i * 2);
        if (i < count) display.drawBox(x + i * 3, bottom - height + 1, 2, height);
        else display.drawPixel(x + i * 3, bottom);
    }
}

void drawHeader() {
    display.setFont(u8g2_font_4x6_tf);
    display.drawStr(2, 7, "LORA-CW");
    display.drawStr(38, 7, radioReady ? "433" : "RF!");
    if (hasRssi) {
        char rssi[8];
        snprintf(rssi, sizeof(rssi), "%d", lastRssi);
        // Relative strength indicator, not a calibrated link-quality estimate.
        const uint8_t bars = lastRssi >= -75 ? 4 : lastRssi >= -90 ? 3 :
                             lastRssi >= -105 ? 2 : 1;
        drawBars(57, 7, bars);
        display.drawStr(72, 7, rssi);
    } else display.drawStr(58, 7, "RX --");
    display.drawStr(105, 7, "W");
    if (wifiConnected) drawBars(114, 7, 4);
    else if (wifiAttempting) drawBars(114, 7, 1 + (millis() / 250) % 4);
    else {
        display.drawLine(116, 2, 122, 7);
        display.drawLine(122, 2, 116, 7);
    }
    display.drawHLine(0, 9, SCREEN_W);
}

void drawCard(int x, const char* label, const Marks& marks, char last, bool lit) {
    display.drawRFrame(x, 12, 61, 24, 2);
    if (lit) display.drawRBox(x + 3, 14, 13, 7, 1);
    display.setFont(u8g2_font_4x6_tf);
    display.setDrawColor(lit ? 0 : 1);
    display.drawStr(x + 5, 20, label);
    display.setDrawColor(1);
    if (marks.length) {
        int markX = x + 4;
        for (uint8_t i = 0; i < marks.length; ++i) {
            const uint8_t width = marks.text[i] == '-' ? 4 : 2;
            display.drawBox(markX, 29, width, 2);
            markX += width + 2;
        }
    } else {
        display.drawStr(x + 4, 31, last ? "LAST" : (label[0] == 'T' ? "READY" : "WAIT"));
    }
    const char candidate = marks.length ? decodeMorse(marks) : last;
    if (candidate == ' ') {
        display.setFont(u8g2_font_5x7_tf);
        centered("SP", x + 40, 31, 18);
    } else if (candidate) {
        char text[2] = {candidate, '\0'};
        display.setFont(u8g2_font_logisoso16_tf);
        // Clip to this card's letter region, even if a replacement font is wider.
        display.setClipWindow(x + 40, 13, x + 60, 35);
        centered(text, x + 40, 33, 19);
        display.setMaxClipWindow();
    } else display.drawHLine(x + 47, 27, 6);
}

void drawProgress(uint32_t now) {
    display.drawHLine(2, 39, 124);
    uint32_t elapsed = 0, total = 1;
    bool visible = true;
    if (control.down && !controlHandled) {
        elapsed = now - controlStartedAt; total = CONTROL_HOLD_MS;
    } else if (key.down && !ignoreKeyUntilRelease) {
        elapsed = now - keyStartedAt; total = DOT_DASH_SPLIT_MS;
    } else if (outgoing.length) {
        elapsed = now - lastMarkAt; total = CHARACTER_PAUSE_MS;
    } else visible = false;
    if (visible) {
        if (elapsed > total) elapsed = total;
        const uint8_t width = uint8_t(elapsed * 124UL / total);
        if (width) display.drawBox(2, 37, width, 2);
    }
}

void drawLog() {
    display.setFont(u8g2_font_5x7_tf);
    if (!receivedLength) {
        centered("NO MESSAGE YET", 2, 47, 124);
        display.setFont(u8g2_font_4x6_tf);
        centered("KEY TO SEND", 2, 54, 124);
        return;
    }
    for (uint8_t row = 0; row < 2; ++row) {
        const uint8_t offset = row * LOG_COLS;
        if (offset >= receivedLength) break;
        uint8_t count = receivedLength - offset;
        if (count > LOG_COLS) count = LOG_COLS;
        char line[LOG_COLS + 1];
        memcpy(line, receivedText + offset, count);
        line[count] = '\0';
        display.drawStr(2, 47 + row * 7, line);
    }
}

void drawFooter(uint32_t now) {
    display.drawHLine(0, 56, SCREEN_W);
    display.setFont(u8g2_font_4x6_tf);
    char text[32];
    if (!radioReady) snprintf(text, sizeof(text), "RADIO OFFLINE - CHECK WIRING");
    else if (control.down && !controlHandled) snprintf(text, sizeof(text), "HOLD: SPACE / TAP: CLEAR");
    else if (key.down && !ignoreKeyUntilRelease) {
        const uint32_t duration = now - keyStartedAt;
        snprintf(text, sizeof(text), "%s  %lums", duration < DOT_DASH_SPLIT_MS ? "DOT" : "DASH",
                 static_cast<unsigned long>(duration));
    } else if (statusActive) snprintf(text, sizeof(text), "%s", statusText);
    else if (txCount || txBusy) snprintf(text, sizeof(text), "SENDING  %u QUEUED", unsigned(txCount));
    else if (footerShowsIp && wifiConnected) snprintf(text, sizeof(text), "IP %s", ipText);
    else snprintf(text, sizeof(text), "CTRL: TAP CLEAR / HOLD SPACE");
    // A single full-width lane prevents status/IP overlap.
    centered(text, 2, 63, 124);
}

void drawUi(uint32_t now) {
    display.clearBuffer();
    display.setDrawColor(1);
    display.setFontMode(1);
    drawHeader();
    drawCard(2, "TX", outgoing, lastTxChar, txPulse || (key.down && !ignoreKeyUntilRelease));
    drawCard(65, "RX", incoming, lastRxChar, rxPulse);
    drawProgress(now);
    drawLog();
    drawFooter(now);
}

void flushDisplayStep() {
    const uint8_t* buffer = display.getBufferPtr();
    // One short contiguous tile run per loop; key and RF get serviced between runs.
    while (nextTile < TILE_COUNT) {
        const uint16_t start = nextTile;
        if (!memcmp(buffer + start * 8, previousFrame + start * 8, 8)) {
            ++nextTile;
            continue;
        }
        uint8_t count = 1;
        while (count < TILES_PER_TRANSFER && start + count < TILE_COUNT &&
               (start + count) / TILES_X == start / TILES_X &&
               memcmp(buffer + (start + count) * 8, previousFrame + (start + count) * 8, 8)) {
            ++count;
        }
        display.updateDisplayArea(start % TILES_X, start / TILES_X, count, 1);
        memcpy(previousFrame + start * 8, buffer + start * 8, count * 8);
        nextTile += count;
        return;
    }
}

void serviceDisplay(uint32_t now) {
    if (otaActive) return;
    if (nextTile < TILE_COUNT) { flushDisplayStep(); return; }
    const bool animated = (key.down && !ignoreKeyUntilRelease) || outgoing.length ||
                          (control.down && !controlHandled) || wifiAttempting;
    if ((!screenDirty && !animated) || uint32_t(now - frameAt) < UI_FRAME_MS) return;
    drawUi(now);
    screenDirty = false;
    frameAt = now;
    nextTile = 0;
    flushDisplayStep();
}

void drawOtaScreen(const char* label) {
    display.clearBuffer();
    display.setDrawColor(1);
    display.setFont(u8g2_font_5x7_tf);
    centered(label, 0, 15, 128);
    char percent[8];
    snprintf(percent, sizeof(percent), "%u%%", unsigned(otaPercent));
    display.setFont(u8g2_font_logisoso16_tf);
    centered(percent, 0, 39, 128);
    display.drawFrame(10, 47, 108, 7);
    const uint8_t fill = uint8_t(106UL * otaPercent / 100);
    if (fill) display.drawBox(11, 48, fill, 5);
    display.setFont(u8g2_font_4x6_tf);
    centered("KEEP POWER ON", 0, 63, 128);
    // Full frames are appropriate here: normal key/radio work is suspended.
    display.sendBuffer();
    memcpy(previousFrame, display.getBufferPtr(), FRAME_BYTES);
    nextTile = TILE_COUNT;
    frameAt = millis();
}

// WiFi and OTA --------------------------------------------------------------
void configureOTA() {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    if (OTA_PASSWORD[0]) ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]() {
        otaActive = true;
        otaPercent = 0;
        stopOutputs();
        txCount = txHead = txTail = 0;
        txBusy = false;
        outgoing.clear();
        incoming.clear();
        rxDiscarding = false;
        if (radioReady) LoRa.idle();
        drawOtaScreen("UPDATING FIRMWARE");
        Serial.println("[OTA] Starting");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        if (!total) return;
        uint32_t percent = uint32_t(uint64_t(progress) * 100ULL / total);
        if (percent > 100) percent = 100;
        if (percent == otaPercent) return;
        otaPercent = uint8_t(percent);
        if (uint32_t(millis() - frameAt) >= 100 || otaPercent == 100)
            drawOtaScreen("UPDATING FIRMWARE");
    });
    ArduinoOTA.onEnd([]() {
        otaPercent = 100;
        drawOtaScreen("UPDATE COMPLETE");
        Serial.println("[OTA] Complete; restarting");
        // Leave otaActive true: the library reboots after this callback.
    });
    ArduinoOTA.onError([](ota_error_t error) {
        const bool interrupted = otaActive;
        otaActive = false;
        if (interrupted) {
            resetInputs(millis());
            if (radioReady) queueToken('/', '\0', false);  // Restore the peer's boundary.
        }
        char message[32];
        snprintf(message, sizeof(message), "OTA ERROR %u", unsigned(error));
        setStatus(message);
        Serial.println(message);
    });
}

void startWiFiAttempt(uint32_t now) {
    wifiAttempting = true;
    wifiAttemptAt = now;
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    screenDirty = true;
    Serial.println("[WiFi] Connecting in background");
}

void maintainWiFi(uint32_t now) {
    if (uint32_t(now - wifiPollAt) < WIFI_POLL_MS) return;
    wifiPollAt = now;
    const bool connected = WiFi.status() == WL_CONNECTED;
    if (connected) {
        const IPAddress ip = WiFi.localIP();
        char currentIp[16];
        snprintf(currentIp, sizeof(currentIp), "%u.%u.%u.%u",
                 unsigned(ip[0]), unsigned(ip[1]), unsigned(ip[2]), unsigned(ip[3]));
        if (!wifiConnected || strcmp(currentIp, ipText)) {
            wifiConnected = true;
            wifiAttempting = false;
            snprintf(ipText, sizeof(ipText), "%s", currentIp);
            if (otaStarted) ArduinoOTA.end();
            ArduinoOTA.begin();  // Start only after the interface has connected.
            otaStarted = true;
            screenDirty = true;
            Serial.print("[WiFi] IP: "); Serial.println(ipText);
        }
        return;
    }
    if (wifiConnected) {
        wifiConnected = false;
        ipText[0] = '\0';
        if (otaStarted) ArduinoOTA.end();
        otaStarted = false;
        wifiAttempting = false;
        wifiRetryAt = now;
        screenDirty = true;
        Serial.println("[WiFi] Connection lost");
    }
    if (wifiAttempting && uint32_t(now - wifiAttemptAt) >= WIFI_CONNECT_TIMEOUT_MS) {
        WiFi.disconnect(false, false);
        wifiAttempting = false;
        wifiRetryAt = now;
        screenDirty = true;
        Serial.println("[WiFi] Attempt timed out; Morse remains available");
    } else if (!wifiAttempting && uint32_t(now - wifiRetryAt) >= WIFI_RETRY_INTERVAL_MS) {
        startWiFiAttempt(now);
    }
}

// Entry points --------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    pinMode(KEY_PIN, INPUT_PULLUP);
    pinMode(CONTROL_PIN, INPUT_PULLUP);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(TX_LED_PIN, OUTPUT);
    pinMode(RX_LED_PIN, OUTPUT);
    stopOutputs();
    resetInputs(millis());

    Wire.begin(OLED_SDA, OLED_SCL);
    display.setBusClock(OLED_I2C_HZ);
    display.begin();
    display.setContrast(100);
    display.setFontMode(1);
    display.clearBuffer();
    display.setFont(u8g2_font_logisoso16_tf);
    centered("LORA-CW", 0, 28, 128);
    display.setFont(u8g2_font_5x7_tf);
    centered("STARTING RADIO", 0, 47, 128);
    display.sendBuffer();
    memcpy(previousFrame, display.getBufferPtr(), FRAME_BYTES);

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
    radioReady = LoRa.begin(LORA_FREQUENCY) != 0;
    if (radioReady) LoRa.parsePacket();  // Arm reception immediately.

    configureOTA();
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(false);  // One explicit retry state machine.
    startWiFiAttempt(millis());
    resetInputs(millis());
    setStatus(radioReady ? "READY - WIFI CONNECTING" : "RADIO INIT FAILED");
    frameAt = millis() - UI_FRAME_MS;
    footerAt = millis();
    Serial.println(radioReady ? "[LoRa] Ready" : "[LoRa] Initialization failed");
}

void loop() {
    if (!otaActive) {
        serviceInputs(millis());
        serviceRadio(millis());
        serviceOutputs(millis());
        serviceTimers(millis());
        maintainWiFi(millis());
    }
    if (wifiConnected && otaStarted) ArduinoOTA.handle();
    serviceDisplay(millis());
    yield();
}
