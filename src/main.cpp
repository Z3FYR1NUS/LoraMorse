#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <LoRa.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <ArduinoOTA.h>

// -----------------------------------------------------------------------------
// Network & OTA Configuration
// -----------------------------------------------------------------------------

const char *WIFI_SSID = "";
const char *WIFI_PASS = "";
const char *OTA_HOSTNAME = "ESP32-Morse-Station";

// -----------------------------------------------------------------------------
// Hardware Configuration
// -----------------------------------------------------------------------------

constexpr long LORA_FREQUENCY = 433E6;

constexpr uint16_t DOT_DASH_SPLIT_MS = 250;
constexpr uint16_t CHARACTER_PAUSE_MS = 850;

constexpr uint16_t TX_FLASH_MS = 90;
constexpr uint16_t RX_FLASH_MS = 120;

constexpr uint16_t BEEP_DOT_MS = 55;
constexpr uint16_t BEEP_DASH_MS = 170;

constexpr uint16_t UI_FRAME_MS = 40;
constexpr uint16_t ACTIVITY_ANIM_MS = 350;
constexpr uint16_t STATUS_HOLD_MS = 1500;

// GPIO Definitions
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

constexpr int KEY_PIN = 4;
constexpr int CONTROL_PIN = 14;

// -----------------------------------------------------------------------------
// Display Driver (SH1106 / SSD1306-class 1.3" 128x64 white panel)
// -----------------------------------------------------------------------------

U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
    U8G2_R0,
    U8X8_PIN_NONE
);

// -----------------------------------------------------------------------------
// UI Layout Constants
//
// All spacing lives here so every panel shares the same margins, gutters and
// padding instead of each draw function inventing its own offsets. This is
// what previously caused the inconsistent card padding, the boxes butting
// directly against the activity bar, and text touching frame walls.
// -----------------------------------------------------------------------------

constexpr int SCREEN_W = 128;
constexpr int SCREEN_H = 64;

constexpr int MARGIN = 3;   // gap from the physical screen edge to any element
constexpr int GUTTER = 4;   // gap between two adjacent elements/panels
constexpr int PADDING = 3;  // gap between a panel's border and its own content

// Header band
constexpr int HEADER_H = 11;

// TX / RX cards (side by side, directly under the header)
constexpr int CARD_Y = HEADER_H + 3;
constexpr int CARD_H = 21;
constexpr int CARD_W = (SCREEN_W - (2 * MARGIN) - GUTTER) / 2; // 59
constexpr int TX_CARD_X = MARGIN;
constexpr int RX_CARD_X = TX_CARD_X + CARD_W + GUTTER;

// Activity bar (below the cards)
constexpr int ACTIVITY_Y = CARD_Y + CARD_H + GUTTER;
constexpr int ACTIVITY_H = 4;
constexpr int ACTIVITY_X = MARGIN;
constexpr int ACTIVITY_W = SCREEN_W - (2 * MARGIN);

// Conversation log line
constexpr int LOG_BASELINE_Y = ACTIVITY_Y + ACTIVITY_H + GUTTER + 7;

// Footer divider + status line
constexpr int FOOTER_DIVIDER_Y = LOG_BASELINE_Y + 4;
constexpr int FOOTER_BASELINE_Y = SCREEN_H - 2;

// -----------------------------------------------------------------------------
// Global State
// -----------------------------------------------------------------------------

char outgoingMarks[7] = "";
char receivedMarks[7] = "";
char receivedText[22] = "";

char statusText[20] = "INITIALIZING";

bool keyWasDown = false;
bool controlWasDown = false;
bool radioReady = false;
bool wifiConnected = false;
bool screenDirty = true;

bool txActivity = false;
bool rxActivity = false;

unsigned long keyStartedAt = 0;
unsigned long lastMarkAt = 0;

unsigned long txLedUntil = 0;
unsigned long rxLedUntil = 0;
unsigned long buzzerUntil = 0;

unsigned long activityStartedAt = 0;
unsigned long statusChangedAt = 0;
unsigned long lastUiFrameAt = 0;

// -----------------------------------------------------------------------------
// Forward Declarations
// -----------------------------------------------------------------------------

void drawCenteredText(const char *text, int x, int y, int width);
void drawRightAlignedText(const char *text, int rightEdge, int y);
void drawFitText(const char *text, int x, int y, int width);
void drawHeader();
void drawBufferCards();
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

char decodeMorse(const char *marks);
void appendMark(char *marks, char mark);
void appendReceivedLetter(char letter);
void setStatus(const char *text);
void initWiFiAndOTA();

// -----------------------------------------------------------------------------
// Morse Decoding Table
// -----------------------------------------------------------------------------

char decodeMorse(const char *marks) {
  struct MorseEntry {
    const char *code;
    char letter;
  };

  static const MorseEntry table[] = {
      {".-", 'A'},   {"-...", 'B'}, {"-.-.", 'C'}, {"-..", 'D'},  {".", 'E'},
      {"..-.", 'F'},  {"--.", 'G'},  {"....", 'H'}, {"..", 'I'},   {".---", 'J'},
      {"-.-", 'K'},   {".-..", 'L'}, {"--", 'M'},   {"-.", 'N'},   {"---", 'O'},
      {".--.", 'P'},  {"--.-", 'Q'}, {".-.", 'R'},  {"...", 'S'},  {"-", 'T'},
      {"..-", 'U'},   {"...-", 'V'}, {".--", 'W'},  {"-..-", 'X'}, {"-.--", 'Y'},
      {"--..", 'Z'},  {"-----", '0'},{".----", '1'},{"..---", '2'},{"...--", '3'},
      {"....-", '4'}, {".....", '5'},{"-....", '6'},{"--...", '7'},{"---..", '8'},
      {"----.", '9'}
  };

  for (const MorseEntry &entry : table) {
    if (strcmp(marks, entry.code) == 0) {
      return entry.letter;
    }
  }

  return '?';
}

// -----------------------------------------------------------------------------
// Buffer Management
// -----------------------------------------------------------------------------

void appendMark(char *marks, char mark) {
  const size_t length = strlen(marks);

  if (length >= 6) {
    marks[0] = '\0';
    return;
  }

  marks[length] = mark;
  marks[length + 1] = '\0';
}

void appendReceivedLetter(char letter) {
  const size_t length = strlen(receivedText);

  if (length < sizeof(receivedText) - 1) {
    receivedText[length] = letter;
    receivedText[length + 1] = '\0';
    return;
  }

  memmove(receivedText, receivedText + 1, sizeof(receivedText) - 2);
  receivedText[sizeof(receivedText) - 2] = letter;
  receivedText[sizeof(receivedText) - 1] = '\0';
}

void setStatus(const char *text) {
  strncpy(statusText, text, sizeof(statusText) - 1);
  statusText[sizeof(statusText) - 1] = '\0';
  statusChangedAt = millis();
  screenDirty = true;
}

// -----------------------------------------------------------------------------
// UI Helper Functions
// -----------------------------------------------------------------------------

void drawCenteredText(const char *text, int x, int y, int width) {
  int textWidth = display.getStrWidth(text);
  int textX = x + (width - textWidth) / 2;
  if (textX < x) textX = x;
  display.drawStr(textX, y, text);
}

// Right-aligns text against a given edge instead of a hardcoded x position,
// so variable-length strings (IP addresses, frequencies) never run off the
// panel the way the old fixed-offset draw did.
void drawRightAlignedText(const char *text, int rightEdge, int y) {
  int textWidth = display.getStrWidth(text);
  int textX = rightEdge - textWidth;
  if (textX < 0) textX = 0;
  display.drawStr(textX, y, text);
}

void drawFitText(const char *text, int x, int y, int width) {
  char buffer[12];
  strncpy(buffer, text, sizeof(buffer) - 1);
  buffer[sizeof(buffer) - 1] = '\0';

  while (strlen(buffer) > 1 && display.getStrWidth(buffer) > width) {
    buffer[strlen(buffer) - 1] = '\0';
  }

  drawCenteredText(buffer, x, y, width);
}

// -----------------------------------------------------------------------------
// UI Rendering Modules
// -----------------------------------------------------------------------------

void drawHeader() {
  display.setDrawColor(1);
  display.drawBox(0, 0, SCREEN_W, HEADER_H);

  display.setDrawColor(0);
  display.setFont(u8g2_font_micro_tr);
  display.drawStr(MARGIN, HEADER_H - 3, "LORA-CW");

  // Right-aligned so "WIFI OK" and "NO WIFI" sit at the same visual edge
  // instead of two slightly different hardcoded x positions.
  drawRightAlignedText(wifiConnected ? "WIFI OK" : "NO WIFI",
                        SCREEN_W - MARGIN, HEADER_H - 3);

  display.setDrawColor(1);
  display.drawHLine(0, HEADER_H, SCREEN_W);
}

void drawBufferCards() {
  // TX Card Panel
  display.drawRFrame(TX_CARD_X, CARD_Y, CARD_W, CARD_H, 2);
  display.setFont(u8g2_font_4x6_tf);
  display.drawStr(TX_CARD_X + PADDING, CARD_Y + PADDING + 4, "TX");

  display.setFont(u8g2_font_7x14_tf);
  const int txContentY = CARD_Y + CARD_H - PADDING - 2;
  if (outgoingMarks[0]) {
    drawFitText(outgoingMarks, TX_CARD_X + PADDING, txContentY, CARD_W - (2 * PADDING));
  } else {
    drawCenteredText(".", TX_CARD_X + PADDING, txContentY, CARD_W - (2 * PADDING));
  }

  // RX Card Panel — identical padding math to TX so the two panels match.
  display.drawRFrame(RX_CARD_X, CARD_Y, CARD_W, CARD_H, 2);
  display.setFont(u8g2_font_4x6_tf);
  display.drawStr(RX_CARD_X + PADDING, CARD_Y + PADDING + 4, "RX");

  display.setFont(u8g2_font_7x14_tf);
  const int rxContentY = CARD_Y + CARD_H - PADDING - 2;
  if (receivedMarks[0]) {
    drawFitText(receivedMarks, RX_CARD_X + PADDING, rxContentY, CARD_W - (2 * PADDING));
  } else {
    drawCenteredText("-", RX_CARD_X + PADDING, rxContentY, CARD_W - (2 * PADDING));
  }
}

void drawActivityBar() {
  const unsigned long now = millis();
  const bool active = txActivity || rxActivity;

  display.drawFrame(ACTIVITY_X, ACTIVITY_Y, ACTIVITY_W, ACTIVITY_H);

  const int trackWidth = ACTIVITY_W - 6; // inset so the moving box never clips the frame
  if (active) {
    const unsigned long elapsed = now - activityStartedAt;
    uint8_t pos = (elapsed >= ACTIVITY_ANIM_MS)
                  ? trackWidth
                  : static_cast<uint8_t>((elapsed * static_cast<unsigned long>(trackWidth)) / ACTIVITY_ANIM_MS);

    display.drawBox(ACTIVITY_X + 2 + pos, ACTIVITY_Y + 1, 4, ACTIVITY_H - 2);
  } else if (radioReady) {
    display.drawBox(ACTIVITY_X + 2, ACTIVITY_Y + 1, 12, ACTIVITY_H - 2);
  }
}

void drawConversationLog() {
  display.setFont(u8g2_font_6x10_tf);

  if (!receivedText[0]) {
    display.drawStr(MARGIN + 1, LOG_BASELINE_Y, "READY TO KEY...");
  } else {
    drawFitText(receivedText, MARGIN, LOG_BASELINE_Y, SCREEN_W - (2 * MARGIN));
  }
}

void drawFooterStatus() {
  const unsigned long now = millis();

  display.drawHLine(0, FOOTER_DIVIDER_Y, SCREEN_W);
  display.setFont(u8g2_font_4x6_tf);

  if (statusText[0] && (now - statusChangedAt < STATUS_HOLD_MS)) {
    display.drawStr(MARGIN, FOOTER_BASELINE_Y, statusText);
  } else {
    display.drawStr(MARGIN, FOOTER_BASELINE_Y, radioReady ? "LINK ACTIVE" : "RADIO FAIL");
  }

  // Right-aligned against the true screen edge — fixes the truncated IP,
  // and scales to any address length or to "433MHz" without retuning an
  // x offset by hand.
  if (wifiConnected) {
    drawRightAlignedText(WiFi.localIP().toString().c_str(), SCREEN_W - MARGIN, FOOTER_BASELINE_Y);
  } else {
    drawRightAlignedText("433MHz", SCREEN_W - MARGIN, FOOTER_BASELINE_Y);
  }
}

void drawUi() {
  display.clearBuffer();

  drawHeader();
  drawBufferCards();
  drawActivityBar();
  drawConversationLog();
  drawFooterStatus();

  display.sendBuffer();
  screenDirty = false;
}

void serviceDisplay() {
  const unsigned long now = millis();
  bool active = txActivity || rxActivity;

  if (active && (now - activityStartedAt >= ACTIVITY_ANIM_MS)) {
    txActivity = false;
    rxActivity = false;
    screenDirty = true;
    active = false;
  }

  if (screenDirty || (active && (now - lastUiFrameAt >= UI_FRAME_MS))) {
    drawUi();
    lastUiFrameAt = now;
  }
}

// -----------------------------------------------------------------------------
// Radio Control & Telemetry
// -----------------------------------------------------------------------------

void sendToken(char token) {
  if (!radioReady) return;

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
  if (!outgoingMarks[0]) return;

  const char decoded = decodeMorse(outgoingMarks);
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
  } else if (token == '/' && receivedMarks[0]) {
    appendReceivedLetter(decodeMorse(receivedMarks));
    receivedMarks[0] = '\0';
    setStatus("RX CHAR");
  } else if (token == ' ') {
    appendReceivedLetter(' ');
    setStatus("RX SPACE");
  }
  screenDirty = true;
}

void checkRadio() {
  const int packetSize = LoRa.parsePacket();
  if (!packetSize) return;

  const char token = static_cast<char>(LoRa.read());
  while (LoRa.available()) {
    LoRa.read();
  }

  handleReceivedToken(token);
}

// -----------------------------------------------------------------------------
// Key & Control Handler
// -----------------------------------------------------------------------------

void checkKey() {
  const bool keyDown = (digitalRead(KEY_PIN) == LOW);

  if (keyDown && !keyWasDown) {
    keyStartedAt = millis();
    setStatus("KEY DOWN");
  }

  if (!keyDown && keyWasDown) {
    const char mark = (millis() - keyStartedAt < DOT_DASH_SPLIT_MS) ? '.' : '-';

    appendMark(outgoingMarks, mark);
    sendToken(mark);
    startTransmitActivity();

    lastMarkAt = millis();
    setStatus(mark == '.' ? "TX DOT" : "TX DASH");
  }

  keyWasDown = keyDown;

  if (!keyDown && outgoingMarks[0] && (millis() - lastMarkAt >= CHARACTER_PAUSE_MS)) {
    finishOutgoingCharacter();
  }
}

void checkControl() {
  const bool controlDown = (digitalRead(CONTROL_PIN) == LOW);

  if (controlDown && !controlWasDown) {
    outgoingMarks[0] = '\0';
    receivedMarks[0] = '\0';
    receivedText[0] = '\0';
    setStatus("BUFFER CLEARED");
  }

  controlWasDown = controlDown;
}

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

// -----------------------------------------------------------------------------
// Networking & OTA Handler
// -----------------------------------------------------------------------------

void initWiFiAndOTA() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Non-blocking WiFi connection check (up to 3 seconds wait at boot)
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 3000) {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
  }

  ArduinoOTA.setHostname(OTA_HOSTNAME);

  ArduinoOTA.onStart([]() {
    setStatus("OTA STARTING");
    serviceDisplay();
  });

  ArduinoOTA.onEnd([]() {
    setStatus("OTA COMPLETE");
    serviceDisplay();
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    char buf[16];
    snprintf(buf, sizeof(buf), "OTA: %u%%", (progress / (total / 100)));
    setStatus(buf);
    serviceDisplay();
  });

  ArduinoOTA.onError([](ota_error_t error) {
    setStatus("OTA ERROR");
    serviceDisplay();
  });

  ArduinoOTA.begin();
}

// -----------------------------------------------------------------------------
// Core Setup & Main Loop
// -----------------------------------------------------------------------------

void setup() {
  pinMode(KEY_PIN, INPUT_PULLUP);
  pinMode(CONTROL_PIN, INPUT_PULLUP);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TX_LED_PIN, OUTPUT);
  pinMode(RX_LED_PIN, OUTPUT);

  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(TX_LED_PIN, LOW);
  digitalWrite(RX_LED_PIN, LOW);

  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin();
  display.setBusClock(400000);

  initWiFiAndOTA();

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);

  radioReady = LoRa.begin(LORA_FREQUENCY);

  setStatus(radioReady ? "SYSTEM READY" : "LORA INIT FAIL");
  serviceDisplay();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    ArduinoOTA.handle();
  } else {
    wifiConnected = false;
  }

  checkRadio();
  checkKey();
  checkControl();
  updateOutputs();
  serviceDisplay();
}
