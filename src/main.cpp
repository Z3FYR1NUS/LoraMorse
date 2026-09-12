#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <LoRa.h>
#include <U8g2lib.h>

// Flash this same program to both Morse stations.


// Forward declarations
void drawSignalIcon(bool active, bool receive);

constexpr long LORA_FREQUENCY = 433E6;
constexpr uint16_t DOT_DASH_SPLIT_MS = 250;
constexpr uint16_t CHARACTER_PAUSE_MS = 850;
constexpr uint16_t TX_FLASH_MS = 90;
constexpr uint16_t RX_FLASH_MS = 120;
constexpr uint16_t BEEP_DOT_MS = 55;
constexpr uint16_t BEEP_DASH_MS = 170;
constexpr uint16_t UI_FRAME_MS = 50;       // 20 frames/second only during activity.
constexpr uint16_t ACTIVITY_ANIM_MS = 420;
constexpr uint16_t STATUS_HOLD_MS = 1400;

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

// Full-buffer mode gives this SH1106 panel crisp fonts and flicker-free frames.
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

char outgoingMarks[7] = "";
char receivedMarks[7] = "";
char receivedText[20] = "";
char statusText[17] = "BOOTING";

bool keyWasDown = false;
bool controlWasDown = false;
bool radioReady = false;
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

char decodeMorse(const char *marks);
void appendMark(char *marks, char mark);
void appendReceivedLetter(char letter);
void setStatus(const char *text);
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

char decodeMorse(const char *marks) {
  struct MorseEntry { const char *code; char letter; };
  static const MorseEntry table[] = {
    {".-", 'A'}, {"-...", 'B'}, {"-.-.", 'C'}, {"-..", 'D'}, {".", 'E'},
    {"..-.", 'F'}, {"--.", 'G'}, {"....", 'H'}, {"..", 'I'}, {".---", 'J'},
    {"-.-", 'K'}, {".-..", 'L'}, {"--", 'M'}, {"-.", 'N'}, {"---", 'O'},
    {".--.", 'P'}, {"--.-", 'Q'}, {".-.", 'R'}, {"...", 'S'}, {"-", 'T'},
    {"..-", 'U'}, {"...-", 'V'}, {".--", 'W'}, {"-..-", 'X'}, {"-.--", 'Y'},
    {"--..", 'Z'}, {"-----", '0'}, {".----", '1'}, {"..---", '2'}, {"...--", '3'},
    {"....-", '4'}, {".....", '5'}, {"-....", '6'}, {"--...", '7'}, {"---..", '8'},
    {"----.", '9'}
  };
  for (const MorseEntry &entry : table) {
    if (strcmp(marks, entry.code) == 0) return entry.letter;
  }
  return '?';
}

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
  } else {
    memmove(receivedText, receivedText + 1, sizeof(receivedText) - 2);
    receivedText[sizeof(receivedText) - 2] = letter;
    receivedText[sizeof(receivedText) - 1] = '\0';
  }
}

void setStatus(const char *text) {
  strncpy(statusText, text, sizeof(statusText) - 1);
  statusText[sizeof(statusText) - 1] = '\0';
  statusChangedAt = millis();
  screenDirty = true;
}

void drawSignalIcon(bool active, bool receive) {
  const uint8_t x = 112;
  const uint8_t y = 5;
  if (!radioReady) {
    display.drawCircle(x + 5, y + 3, 3);
    return;
  }

  display.drawDisc(x + 1, y + 6, 1);
  display.drawLine(x + 3, y + 6, x + 5, y + 4);
  display.drawLine(x + 5, y + 4, x + 7, y + 6);
  if (active) {
    const uint8_t wave = (millis() - activityStartedAt) / 90 % 3;
    // U8g2 arcs use one circular radius and 0–255 angle units.
    display.drawArc(x + 5, y + 6, 5 + wave, 150, 234);
    if (receive) display.drawDisc(x + 5, y + 6, 2);
  }
}

void drawUi() {
  const unsigned long now = millis();
  const bool active = txActivity || rxActivity;

  display.clearBuffer();

  // Header uses only fixed, short labels so nothing can collide with the icon.
  display.drawBox(0, 0, 128, 13);
  display.setDrawColor(0);
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(4, 10, "MORSE LINK");
  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(79, 9, "433M");
  display.setDrawColor(1);
  drawSignalIcon(active, rxActivity);

  // Live send card: a large mark makes dots and dashes readable at a glance.
  display.drawRFrame(3, 16, 59, 25, 3);
  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(7, 24, "SEND");
  display.setFont(u8g2_font_10x20_tf);
  display.drawStr(8, 39, outgoingMarks[0] ? outgoingMarks : "_");

  // Live receive card.
  display.drawRFrame(66, 16, 59, 25, 3);
  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(70, 24, "RECV");
  display.setFont(u8g2_font_10x20_tf);
  display.drawStr(71, 39, receivedMarks[0] ? receivedMarks : "-");

  // Thin travelling activity bar: it animates only during a send or receive event.
  display.drawFrame(3, 45, 122, 5);
  if (active) {
    const uint8_t position = ((now - activityStartedAt) * 118UL) / ACTIVITY_ANIM_MS;
    display.drawBox(5 + min<uint8_t>(position, 116), 46, 4, 3);
  } else if (radioReady) {
    display.drawBox(5, 46, 18, 3);
  }

  // One dedicated bottom line: never draw a status label on top of this text.
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(4, 61, receivedText[0] ? receivedText : "Tap key to send");

  display.sendBuffer();
  screenDirty = false;
}

void serviceDisplay() {
  const unsigned long now = millis();
  const bool active = txActivity || rxActivity;
  if (active && now - activityStartedAt >= ACTIVITY_ANIM_MS) {
    txActivity = false;
    rxActivity = false;
    screenDirty = true;
  }
  if (screenDirty || (active && now - lastUiFrameAt >= UI_FRAME_MS)) {
    drawUi();
    lastUiFrameAt = now;
  }
}

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
  char decoded = decodeMorse(outgoingMarks);
  sendToken('/');
  startTransmitActivity();
  outgoingMarks[0] = '\0';
  char message[16];
  snprintf(message, sizeof(message), "SENT %c", decoded);
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
    setStatus("LETTER IN");
  } else if (token == ' ') {
    appendReceivedLetter(' ');
    setStatus("SPACE IN");
  }
  screenDirty = true;
}

void checkRadio() {
  const int packetSize = LoRa.parsePacket();
  if (!packetSize) return;
  const char token = static_cast<char>(LoRa.read());
  while (LoRa.available()) LoRa.read();
  handleReceivedToken(token);
}

void checkKey() {
  const bool keyDown = digitalRead(KEY_PIN) == LOW;
  if (keyDown && !keyWasDown) {
    keyStartedAt = millis();
    setStatus("KEY DOWN");
  }
  if (!keyDown && keyWasDown) {
    const char mark = millis() - keyStartedAt < DOT_DASH_SPLIT_MS ? '.' : '-';
    appendMark(outgoingMarks, mark);
    sendToken(mark);
    startTransmitActivity();
    lastMarkAt = millis();
    setStatus(mark == '.' ? "TX DOT" : "TX DASH");
  }
  keyWasDown = keyDown;

  if (!keyDown && outgoingMarks[0] && millis() - lastMarkAt >= CHARACTER_PAUSE_MS) {
    finishOutgoingCharacter();
  }
}

void checkControl() {
  const bool controlDown = digitalRead(CONTROL_PIN) == LOW;
  if (controlDown && !controlWasDown) {
    outgoingMarks[0] = '\0';
    receivedMarks[0] = '\0';
    receivedText[0] = '\0';
    setStatus("CLEARED");
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
  // 100 kHz is more tolerant of long breadboard jumpers than fast-mode I2C.
  display.setBusClock(100000);

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
  radioReady = LoRa.begin(LORA_FREQUENCY);
  setStatus(radioReady ? "RADIO READY" : "RADIO ERROR");
  serviceDisplay();
}

void loop() {
  checkRadio();
  checkKey();
  checkControl();
  updateOutputs();
  serviceDisplay();
}
