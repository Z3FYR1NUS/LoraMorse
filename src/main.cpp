#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <LoRa.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <ArduinoOTA.h>

// ============================================================================
// Network configuration
// ============================================================================

const char* WIFI_SSID    = "";
const char* WIFI_PASS    = "";
const char* OTA_HOSTNAME = "";

constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr unsigned long WIFI_RETRY_INTERVAL_MS  = 10000;

// ============================================================================
// Radio & Morse timing
// ============================================================================

constexpr long     LORA_FREQUENCY      = 433E6;

constexpr uint16_t DOT_DASH_SPLIT_MS   = 250;
constexpr uint16_t CHARACTER_PAUSE_MS  = 850;

constexpr uint16_t TX_FLASH_MS         = 90;
constexpr uint16_t RX_FLASH_MS         = 120;

constexpr uint16_t BEEP_DOT_MS         = 55;
constexpr uint16_t BEEP_DASH_MS        = 170;

// ============================================================================
// UI timing
// ============================================================================

constexpr uint16_t UI_FRAME_MS         = 40;
constexpr uint16_t ACTIVITY_ANIM_MS    = 350;
constexpr uint16_t STATUS_HOLD_MS      = 1500;

// ============================================================================
// GPIO map
// ============================================================================

// LoRa SX1278
constexpr int LORA_SCK  = 18;
constexpr int LORA_MISO = 19;
constexpr int LORA_MOSI = 23;
constexpr int LORA_CS   = 16;
constexpr int LORA_RST  = 26;
constexpr int LORA_DIO0 = 25;

// OLED
constexpr int OLED_SDA  = 21;
constexpr int OLED_SCL  = 22;

// User I/O
constexpr int BUZZER_PIN   = 33;
constexpr int TX_LED_PIN   = 32;
constexpr int RX_LED_PIN   = 13;
constexpr int KEY_PIN      = 27;
constexpr int CONTROL_PIN  = 14;

// ============================================================================
// Display
// ============================================================================

U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// ============================================================================
// UI layout constants
// ============================================================================

constexpr int SCREEN_W = 128;
constexpr int SCREEN_H = 64;

constexpr int MARGIN   = 2;
constexpr int GUTTER   = 2;
constexpr int PADDING  = 2;

constexpr int HEADER_H      = 10;
constexpr int HEADER_TEXT_Y = 8;

constexpr int CARD_Y  = 12;
constexpr int CARD_H  = 16;
constexpr int CARD_W  = (SCREEN_W - (2 * MARGIN) - GUTTER) / 2;

constexpr int TX_CARD_X = MARGIN;
constexpr int RX_CARD_X = TX_CARD_X + CARD_W + GUTTER;

constexpr int DETAIL_Y = 36;

constexpr int ACTIVITY_Y = 39;
constexpr int ACTIVITY_H = 3;
constexpr int ACTIVITY_X = MARGIN;
constexpr int ACTIVITY_W = SCREEN_W - (2 * MARGIN);

constexpr int LOG_BASELINE_Y = 52;

constexpr int FOOTER_DIVIDER_Y  = 54;
constexpr int FOOTER_BASELINE_Y = 62;

// ============================================================================
// Runtime state
// ============================================================================

char outgoingMarks[7]  = "";
char receivedMarks[7]  = "";
char receivedText[22]  = "";
char statusText[20]    = "INITIALIZING";

char lastTxChar = '-';
char lastRxChar = '-';

bool keyWasDown     = false;
bool controlWasDown = false;

bool radioReady    = false;
bool wifiConnected = false;
bool screenDirty   = true;

bool txActivity = false;
bool rxActivity = false;

bool hasRssi = false;
int  lastRssi = 0;

unsigned long keyStartedAt     = 0;
unsigned long lastMarkAt       = 0;

unsigned long txLedUntil       = 0;
unsigned long rxLedUntil       = 0;
unsigned long buzzerUntil      = 0;

unsigned long activityStartedAt = 0;
unsigned long statusChangedAt   = 0;
unsigned long lastUiFrameAt     = 0;
unsigned long lastWifiRetryAt   = 0;

// ============================================================================
// Forward declarations
// ============================================================================

char decodeMorse(const char* marks);

void appendMark(char* marks, char mark);
void appendReceivedLetter(char letter);
void setStatus(const char* text);

void drawCenteredText(const char* text, int x, int y, int width);
void drawRightAlignedText(const char* text, int rightEdge, int y);
void drawFitText(const char* text, int x, int y, int width);

void drawBootFrame(uint8_t percent, const char* label);
void drawHeader();
void drawBufferCards();
void drawDetailStrip();
void drawActivityBar();
void drawConversationLog();
void drawFooterStatus();
void drawUi();
void serviceDisplay();

void sendToken(char token);
void startTransmitActivity();
void startReceiveActivity(char token);
void finishOutgoingCharacter();

void handleReceivedToken(char token);
void checkRadio();
void checkKey();
void checkControl();
void updateOutputs();

void initWiFiAndOTA();
void maintainWiFi();

// ============================================================================
// Morse decoder
// ============================================================================

char decodeMorse(const char* marks) {
    struct MorseEntry {
        const char* code;
        char character;
    };

    static const MorseEntry table[] = {
        {".-",    'A'}, {"-...",  'B'}, {"-.-.",  'C'}, {"-..",   'D'},
        {".",     'E'}, {"..-.",  'F'}, {"--.",   'G'}, {"....",  'H'},
        {"..",    'I'}, {".---",  'J'}, {"-.-",   'K'}, {".-..",  'L'},
        {"--",    'M'}, {"-.",    'N'}, {"---",   'O'}, {".--.",  'P'},
        {"--.-",  'Q'}, {".-.",   'R'}, {"...",   'S'}, {"-",     'T'},
        {"..-",   'U'}, {"...-",  'V'}, {".--",   'W'}, {"-..-",  'X'},
        {"-.--",  'Y'}, {"--..",  'Z'},
        {"-----", '0'}, {".----", '1'}, {"..---", '2'}, {"...--", '3'},
        {"....-", '4'}, {".....", '5'}, {"-....", '6'}, {"--...", '7'},
        {"---..", '8'}, {"----.", '9'}
    };

    for (const MorseEntry& entry : table) {
        if (strcmp(marks, entry.code) == 0) {
            return entry.character;
        }
    }

    return '?';
}

// ============================================================================
// Buffer helpers
// ============================================================================

void appendMark(char* marks, char mark) {
    const size_t length = strlen(marks);

    if (length >= 6) {
        return;
    }

    marks[length]     = mark;
    marks[length + 1] = '\0';
}

void appendReceivedLetter(char letter) {
    const size_t length = strlen(receivedText);

    if (length < sizeof(receivedText) - 1) {
        receivedText[length]     = letter;
        receivedText[length + 1] = '\0';
        return;
    }

    // Rolling buffer: drop oldest character
    memmove(receivedText, receivedText + 1, sizeof(receivedText) - 2);
    receivedText[sizeof(receivedText) - 2] = letter;
    receivedText[sizeof(receivedText) - 1] = '\0';
}

void setStatus(const char* text) {
    strncpy(statusText, text, sizeof(statusText) - 1);
    statusText[sizeof(statusText) - 1] = '\0';
    statusChangedAt = millis();
    screenDirty = true;
}

// ============================================================================
// Display helpers
// ============================================================================

void drawCenteredText(const char* text, int x, int y, int width) {
    const int textWidth = display.getStrWidth(text);
    int textX = x + (width - textWidth) / 2;

    if (textX < x) {
        textX = x;
    }

    display.drawStr(textX, y, text);
}

void drawRightAlignedText(const char* text, int rightEdge, int y) {
    const int textWidth = display.getStrWidth(text);
    int textX = rightEdge - textWidth;

    if (textX < 0) {
        textX = 0;
    }

    display.drawStr(textX, y, text);
}

void drawFitText(const char* text, int x, int y, int width) {
    char buffer[22];
    strncpy(buffer, text, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    while (strlen(buffer) > 1 && display.getStrWidth(buffer) > width) {
        buffer[strlen(buffer) - 1] = '\0';
    }

    drawCenteredText(buffer, x, y, width);
}

// ============================================================================
// Boot screen
// ============================================================================

void drawBootFrame(uint8_t percent, const char* label) {
    display.clearBuffer();

    display.setFont(u8g2_font_7x14B_tf);
    drawCenteredText("LORA-CW", 0, 22, SCREEN_W);

    display.setFont(u8g2_font_4x6_tf);
    drawCenteredText(label, 0, 36, SCREEN_W);

    constexpr int barX = 14;
    constexpr int barY = 46;
    constexpr int barW = SCREEN_W - (2 * barX);
    constexpr int barH = 6;

    display.drawFrame(barX, barY, barW, barH);

    const int fillW = ((barW - 2) * percent) / 100;
    if (fillW > 0) {
        display.drawBox(barX + 1, barY + 1, fillW, barH - 2);
    }

    display.sendBuffer();
}

// ============================================================================
// Main UI drawing
// ============================================================================

void drawHeader() {
    display.setFont(u8g2_font_4x6_tf);
    display.drawStr(MARGIN, HEADER_TEXT_Y, "LORA-CW");
    drawRightAlignedText(
        wifiConnected ? "WIFI OK" : "NO WIFI",
        SCREEN_W - MARGIN,
        HEADER_TEXT_Y
    );
    display.drawHLine(0, HEADER_H, SCREEN_W);
}

void drawBufferCards() {
    const int labelY      = CARD_Y + 5;
    const int contentY    = CARD_Y + 13;
    const int contentWidth = CARD_W - (2 * PADDING);

    // ---- TX card ----
    display.drawRFrame(TX_CARD_X, CARD_Y, CARD_W, CARD_H, 2);

    display.setFont(u8g2_font_4x6_tf);
    display.drawStr(TX_CARD_X + PADDING, labelY, "TX");

    display.setFont(u8g2_font_5x8_tf);

    if (outgoingMarks[0]) {
        drawFitText(outgoingMarks, TX_CARD_X + PADDING, contentY, contentWidth);
    } else if (lastTxChar != '-') {
        char txChar[2] = { lastTxChar, '\0' };
        drawCenteredText(txChar, TX_CARD_X + PADDING, contentY, contentWidth);
    } else {
        drawCenteredText("READY", TX_CARD_X + PADDING, contentY, contentWidth);
    }

    // ---- RX card ----
    display.drawRFrame(RX_CARD_X, CARD_Y, CARD_W, CARD_H, 2);

    display.setFont(u8g2_font_4x6_tf);
    display.drawStr(RX_CARD_X + PADDING, labelY, "RX");

    display.setFont(u8g2_font_5x8_tf);

    if (receivedMarks[0]) {
        drawFitText(receivedMarks, RX_CARD_X + PADDING, contentY, contentWidth);
    } else if (lastRxChar != '-') {
        char rxChar[2] = { lastRxChar, '\0' };
        drawCenteredText(rxChar, RX_CARD_X + PADDING, contentY, contentWidth);
    } else {
        drawCenteredText("WAIT", RX_CARD_X + PADDING, contentY, contentWidth);
    }
}

void drawDetailStrip() {
    display.setFont(u8g2_font_4x6_tf);

    if (hasRssi) {
        char rssiText[16];
        snprintf(rssiText, sizeof(rssiText), "RSSI %d dBm", lastRssi);
        display.drawStr(MARGIN, DETAIL_Y, rssiText);
    } else {
        display.drawStr(MARGIN, DETAIL_Y, "RSSI N/A");
    }

    char logText[16];
    snprintf(
        logText,
        sizeof(logText),
        "MSG %u/%u",
        static_cast<unsigned>(strlen(receivedText)),
        static_cast<unsigned>(sizeof(receivedText) - 1)
    );
    drawRightAlignedText(logText, SCREEN_W - MARGIN, DETAIL_Y);
}

void drawActivityBar() {
    const unsigned long now = millis();
    const bool active = txActivity || rxActivity;

    display.drawFrame(ACTIVITY_X, ACTIVITY_Y, ACTIVITY_W, ACTIVITY_H);

    if (!active) {
        return;
    }

    const int trackWidth = ACTIVITY_W - 4;
    if (trackWidth <= 0) {
        return;
    }

    const unsigned long elapsed = now - activityStartedAt;
    int progress;

    if (elapsed >= ACTIVITY_ANIM_MS) {
        progress = trackWidth - 2;
    } else {
        progress = static_cast<int>(
            (elapsed * static_cast<unsigned long>(trackWidth - 2)) / ACTIVITY_ANIM_MS
        );
    }

    if (progress < 0) {
        progress = 0;
    }
    if (progress > trackWidth - 2) {
        progress = trackWidth - 2;
    }

    display.drawBox(ACTIVITY_X + 1 + progress, ACTIVITY_Y + 1, 2, ACTIVITY_H - 2);
}

void drawConversationLog() {
    display.setFont(u8g2_font_5x7_tf);

    if (!receivedText[0]) {
        drawCenteredText(
            "NO MESSAGE YET",
            MARGIN,
            LOG_BASELINE_Y,
            SCREEN_W - (2 * MARGIN)
        );
        return;
    }

    drawFitText(receivedText, MARGIN, LOG_BASELINE_Y, SCREEN_W - (2 * MARGIN));
}

void drawFooterStatus() {
    const unsigned long now = millis();

    display.drawHLine(0, FOOTER_DIVIDER_Y, SCREEN_W);
    display.setFont(u8g2_font_4x6_tf);

    const bool statusVisible =
        statusText[0] && (now - statusChangedAt < STATUS_HOLD_MS);

    if (statusVisible) {
        display.drawStr(MARGIN, FOOTER_BASELINE_Y, statusText);
    } else if (!radioReady) {
        display.drawStr(MARGIN, FOOTER_BASELINE_Y, "RADIO OFFLINE");
    } else if (keyWasDown) {
        display.drawStr(MARGIN, FOOTER_BASELINE_Y, "KEY DOWN");
    } else {
        display.drawStr(MARGIN, FOOTER_BASELINE_Y, "KEY READY");
    }

    if (wifiConnected) {
        const String ip = WiFi.localIP().toString();
        drawRightAlignedText(ip.c_str(), SCREEN_W - MARGIN, FOOTER_BASELINE_Y);
    } else {
        drawRightAlignedText("433 MHz", SCREEN_W - MARGIN, FOOTER_BASELINE_Y);
    }
}

void drawUi() {
    display.clearBuffer();

    drawHeader();
    drawBufferCards();
    drawDetailStrip();
    drawActivityBar();
    drawConversationLog();
    drawFooterStatus();

    display.sendBuffer();
    screenDirty = false;
}

void serviceDisplay() {
    const unsigned long now = millis();
    bool active = txActivity || rxActivity;

    if (active && now - activityStartedAt >= ACTIVITY_ANIM_MS) {
        txActivity = false;
        rxActivity = false;
        screenDirty = true;
        active = false;
    }

    if (screenDirty || (active && now - lastUiFrameAt >= UI_FRAME_MS)) {
        drawUi();
        lastUiFrameAt = now;
    }
}

// ============================================================================
// LoRa helpers
// ============================================================================

void sendToken(char token) {
    if (!radioReady) {
        return;
    }

    LoRa.beginPacket();
    LoRa.write(static_cast<uint8_t>(token));
    LoRa.endPacket();
}

void startTransmitActivity() {
    digitalWrite(TX_LED_PIN, HIGH);
    txLedUntil = millis() + TX_FLASH_MS;

    txActivity = true;
    rxActivity = false;
    activityStartedAt = millis();
    screenDirty = true;
}

void startReceiveActivity(char token) {
    digitalWrite(RX_LED_PIN, HIGH);
    rxLedUntil = millis() + RX_FLASH_MS;

    digitalWrite(BUZZER_PIN, HIGH);
    buzzerUntil = millis() + (token == '-' ? BEEP_DASH_MS : BEEP_DOT_MS);

    rxActivity = true;
    txActivity = false;
    activityStartedAt = millis();
    screenDirty = true;
}

void finishOutgoingCharacter() {
    if (!outgoingMarks[0]) {
        return;
    }

    const char decoded = decodeMorse(outgoingMarks);
    lastTxChar = decoded;

    sendToken('/');
    startTransmitActivity();

    outgoingMarks[0] = '\0';

    char message[16];
    snprintf(message, sizeof(message), "TX: %c", decoded);
    setStatus(message);
}

void handleReceivedToken(char token) {
    if (token == '.' || token == '-') {
        appendMark(receivedMarks, token);
        setStatus(token == '.' ? "RX DOT" : "RX DASH");
        startReceiveActivity(token);
    }
    else if (token == '/' && receivedMarks[0]) {
        const char decoded = decodeMorse(receivedMarks);
        lastRxChar = decoded;
        appendReceivedLetter(decoded);
        receivedMarks[0] = '\0';

        char message[16];
        snprintf(message, sizeof(message), "RX: %c", decoded);
        setStatus(message);
    }
    else if (token == ' ') {
        lastRxChar = ' ';
        appendReceivedLetter(' ');
        setStatus("RX SPACE");
    }

    screenDirty = true;
}

void checkRadio() {
    const int packetSize = LoRa.parsePacket();
    if (!packetSize) {
        return;
    }

    const char token = static_cast<char>(LoRa.read());
    lastRssi = LoRa.packetRssi();
    hasRssi = true;

    // Drain any remaining bytes
    while (LoRa.available()) {
        LoRa.read();
    }

    handleReceivedToken(token);
}

// ============================================================================
// Input handling
// ============================================================================

void checkKey() {
    const unsigned long now = millis();
    const bool keyDown = digitalRead(KEY_PIN) == LOW;

    if (keyDown && !keyWasDown) {
        keyStartedAt = now;
        setStatus("KEY DOWN");
    }

    if (!keyDown && keyWasDown) {
        const unsigned long duration = now - keyStartedAt;
        const char mark = (duration < DOT_DASH_SPLIT_MS) ? '.' : '-';

        appendMark(outgoingMarks, mark);
        sendToken(mark);
        startTransmitActivity();
        lastMarkAt = now;

        setStatus(mark == '.' ? "TX DOT" : "TX DASH");
    }

    keyWasDown = keyDown;

    // Character timeout
    if (!keyDown && outgoingMarks[0] && now - lastMarkAt >= CHARACTER_PAUSE_MS) {
        finishOutgoingCharacter();
    }
}

void checkControl() {
    const bool controlDown = digitalRead(CONTROL_PIN) == LOW;

    if (controlDown && !controlWasDown) {
        outgoingMarks[0] = '\0';
        receivedMarks[0] = '\0';
        receivedText[0]  = '\0';

        lastTxChar = '-';
        lastRxChar = '-';

        hasRssi  = false;
        lastRssi = 0;

        setStatus("BUFFER CLEARED");
    }

    controlWasDown = controlDown;
}

// ============================================================================
// Output timers (LEDs & buzzer)
// ============================================================================

void updateOutputs() {
    const unsigned long now = millis();

    if (txLedUntil && now >= txLedUntil) {
        digitalWrite(TX_LED_PIN, LOW);
        txLedUntil = 0;
    }

    if (rxLedUntil && now >= rxLedUntil) {
        digitalWrite(RX_LED_PIN, LOW);
        rxLedUntil = 0;
    }

    if (buzzerUntil && now >= buzzerUntil) {
        digitalWrite(BUZZER_PIN, LOW);
        buzzerUntil = 0;
    }
}

// ============================================================================
// Wi-Fi & ArduinoOTA
// ============================================================================

void initWiFiAndOTA() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.disconnect(true);
    delay(100);

    Serial.print("[WiFi] Connecting to \"");
    Serial.print(WIFI_SSID);
    Serial.println("\"...");

    WiFi.begin(WIFI_SSID, WIFI_PASS);

    const unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - startAttempt < WIFI_CONNECT_TIMEOUT_MS) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.print("[WiFi] Connected. IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.print("[WiFi] Failed. status()=");
        Serial.println(WiFi.status());
    }

    ArduinoOTA.setHostname(OTA_HOSTNAME);

    ArduinoOTA.onStart([]() {
        setStatus("OTA STARTING");
        serviceDisplay();
        Serial.println("[OTA] Update starting...");
    });

    ArduinoOTA.onEnd([]() {
        setStatus("OTA COMPLETE");
        serviceDisplay();
        Serial.println("[OTA] Update complete.");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        if (total == 0) {
            return;
        }
        const unsigned int percent = (progress * 100U) / total;
        char buffer[20];
        snprintf(buffer, sizeof(buffer), "OTA: %u%%", percent);
        setStatus(buffer);
        serviceDisplay();
    });

    ArduinoOTA.onError([](ota_error_t error) {
        setStatus("OTA ERROR");
        serviceDisplay();
        Serial.print("[OTA] Error: ");
        Serial.println(error);
    });

    ArduinoOTA.begin();
    Serial.println("[OTA] Ready.");
}

void maintainWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!wifiConnected) {
            wifiConnected = true;
            Serial.print("[WiFi] Reconnected. IP: ");
            Serial.println(WiFi.localIP());
            screenDirty = true;
        }
        return;
    }

    if (wifiConnected) {
        wifiConnected = false;
        screenDirty = true;
        Serial.println("[WiFi] Connection lost.");
    }

    const unsigned long now = millis();
    if (now - lastWifiRetryAt >= WIFI_RETRY_INTERVAL_MS) {
        lastWifiRetryAt = now;
        Serial.println("[WiFi] Retrying connection...");
        WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
}

// ============================================================================
// Setup
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println("\n[BOOT] ESP32 Morse Station starting...");

    pinMode(KEY_PIN,     INPUT_PULLUP);
    pinMode(CONTROL_PIN, INPUT_PULLUP);
    pinMode(BUZZER_PIN,  OUTPUT);
    pinMode(TX_LED_PIN,  OUTPUT);
    pinMode(RX_LED_PIN,  OUTPUT);

    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(TX_LED_PIN, LOW);
    digitalWrite(RX_LED_PIN, LOW);

    Wire.begin(OLED_SDA, OLED_SCL);
    display.begin();
    display.setBusClock(100000);
    display.setContrast(100);

    drawBootFrame(15, "DISPLAY OK");
    delay(250);

    drawBootFrame(40, "CONNECTING WIFI");
    initWiFiAndOTA();

    drawBootFrame(70, wifiConnected ? "WIFI OK" : "WIFI OFFLINE");
    delay(250);

    drawBootFrame(90, "STARTING RADIO");

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
    radioReady = LoRa.begin(LORA_FREQUENCY);

    drawBootFrame(100, radioReady ? "SYSTEM READY" : "RADIO FAIL");
    delay(400);

    setStatus(radioReady ? "SYSTEM READY" : "LORA INIT FAIL");
    serviceDisplay();
}

// ============================================================================
// Main loop
// ============================================================================

void loop() {
    maintainWiFi();
    if (wifiConnected) {
        ArduinoOTA.handle();
    }

    checkRadio();
    checkKey();
    checkControl();
    updateOutputs();
    serviceDisplay();
}
