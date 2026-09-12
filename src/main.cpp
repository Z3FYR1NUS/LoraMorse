#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <LoRa.h>
#include <U8g2lib.h>

// ---------- Morse and radio settings ----------
constexpr long LORA_FREQUENCY = 433E6;

constexpr uint16_t DOT_DASH_SPLIT_MS = 250;
constexpr uint16_t CHARACTER_PAUSE_MS = 850;

constexpr uint16_t TX_FLASH_MS = 90;
constexpr uint16_t RX_FLASH_MS = 120;
constexpr uint16_t BEEP_DOT_MS = 55;
constexpr uint16_t BEEP_DASH_MS = 170;

// ---------- ESP32 pin connections ----------
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

// SH1106 128 × 64 OLED over I2C
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

String outgoingMarks;
String receivedMarks;
String receivedText;
String statusText = "STARTING";

bool keyWasDown = false;
bool controlWasDown = false;
bool radioReady = false;

unsigned long keyStartedAt = 0;
unsigned long lastMarkAt = 0;
unsigned long txLedUntil = 0;
unsigned long rxLedUntil = 0;
unsigned long buzzerUntil = 0;

// ---------- Function declarations ----------
char decodeMorse(const String &marks);
void trimText(String &text, size_t limit);
void drawUi();
void sendToken(char token);
void flashTransmit();
void flashReceive(char token);
void finishOutgoingCharacter();
void handleReceivedToken(char token);
void checkRadio();
void checkKey();
void checkControl();
void updateOutputs();

// ---------- Morse decoder ----------
char decodeMorse(const String &marks) {
  struct MorseEntry {
    const char *code;
    char letter;
  };

  static const MorseEntry table[] = {
      {".-", 'A'},    {"-...", 'B'},  {"-.-.", 'C'}, {"-..", 'D'},
      {".", 'E'},     {"..-.", 'F'},  {"--.", 'G'},  {"....", 'H'},
      {"..", 'I'},    {".---", 'J'},  {"-.-", 'K'},  {".-..", 'L'},
      {"--", 'M'},    {"-.", 'N'},    {"---", 'O'},  {".--.", 'P'},
      {"--.-", 'Q'},  {".-.", 'R'},   {"...", 'S'},  {"-", 'T'},
      {"..-", 'U'},   {"...-", 'V'},  {".--", 'W'},  {"-..-", 'X'},
      {"-.--", 'Y'},  {"--..", 'Z'},  {"-----", '0'},
      {".----", '1'}, {"..---", '2'}, {"...--", '3'},
      {"....-", '4'}, {".....", '5'}, {"-....", '6'},
      {"--...", '7'}, {"---..", '8'}, {"----.", '9'}};

  for (const MorseEntry &entry : table) {
    if (marks == entry.code) {
      return entry.letter;
    }
  }

  return '?';
}

void trimText(String &text, size_t limit) {
  while (text.length() > limit) {
    text.remove(0, 1);
  }
}

// ---------- OLED interface ----------
void drawUi() {
  display.clearBuffer();

  // Outer frame and title bar
  display.drawFrame(0, 0, 128, 64);
  display.drawBox(0, 0, 128, 11);

  display.setDrawColor(0);
  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(4, 8, "MORSE // LORA 433");

  display.setDrawColor(1);

  // Outgoing Morse marks
  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(4, 20, "KEY:");

  display.setFont(u8g2_font_7x13B_tf);
  String marks = outgoingMarks.length() ? outgoingMarks : "_";
  display.drawStr(30, 21, marks.c_str());

  // Incoming marks
  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(4, 31, "IN:");

  String incoming = receivedMarks.length() ? receivedMarks : "waiting...";
  display.drawStr(26, 31, incoming.c_str());

  // Received decoded characters
  display.drawHLine(4, 35, 120);
  display.setFont(u8g2_font_6x10_tf);

  String bottom =
      receivedText.length() ? receivedText : "Tap key to transmit";
  trimText(bottom, 19);
  display.drawStr(4, 48, bottom.c_str());

  // Status line and radio indicator
  display.setFont(u8g2_font_5x7_tf);
  display.drawStr(4, 60, statusText.c_str());

  if (radioReady) {
    display.drawDisc(119, 57, 3);
  } else {
    display.drawCircle(119, 57, 3);
  }

  display.sendBuffer();
}

// ---------- LoRa ----------
void sendToken(char token) {
  if (!radioReady) {
    return;
  }

  LoRa.beginPacket();
  LoRa.write(static_cast<uint8_t>(token));
  LoRa.endPacket();
}

void checkRadio() {
  int packetSize = LoRa.parsePacket();

  if (!packetSize) {
    return;
  }

  char token = static_cast<char>(LoRa.read());

  // Ignore any unexpected extra bytes in a packet.
  while (LoRa.available()) {
    LoRa.read();
  }

  handleReceivedToken(token);
}

// ---------- LEDs and buzzer ----------
void flashTransmit() {
  digitalWrite(TX_LED_PIN, HIGH);
  txLedUntil = millis() + TX_FLASH_MS;
}

void flashReceive(char token) {
  digitalWrite(RX_LED_PIN, HIGH);
  rxLedUntil = millis() + RX_FLASH_MS;

  digitalWrite(BUZZER_PIN, HIGH);
  buzzerUntil =
      millis() + (token == '-' ? BEEP_DASH_MS : BEEP_DOT_MS);
}

void updateOutputs() {
  unsigned long now = millis();

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

// ---------- Sending and receiving Morse ----------
void finishOutgoingCharacter() {
  if (!outgoingMarks.length()) {
    return;
  }

  char decoded = decodeMorse(outgoingMarks);

  statusText = String("SENT ") + decoded;

  // Slash means “the current Morse character is complete.”
  sendToken('/');
  flashTransmit();

  outgoingMarks = "";
  drawUi();
}

void handleReceivedToken(char token) {
  if (token == '.' || token == '-') {
    receivedMarks += token;

    // Morse characters are at most a few marks long.
    if (receivedMarks.length() > 6) {
      receivedMarks = "";
    }

    statusText = (token == '.') ? "RX DOT" : "RX DASH";
    flashReceive(token);
  } else if (token == '/') {
    if (receivedMarks.length()) {
      receivedText += decodeMorse(receivedMarks);
      trimText(receivedText, 19);

      receivedMarks = "";
      statusText = "MESSAGE RX";
    }
  } else if (token == ' ') {
    receivedText += ' ';
    trimText(receivedText, 19);
    statusText = "RX SPACE";
  }

  drawUi();
}

// ---------- Buttons ----------
void checkKey() {
  bool keyDown = digitalRead(KEY_PIN) == LOW;

  // The key was just pressed.
  if (keyDown && !keyWasDown) {
    keyStartedAt = millis();
    statusText = "KEY DOWN";
    drawUi();
  }

  // The key was just released.
  if (!keyDown && keyWasDown) {
    unsigned long heldMs = millis() - keyStartedAt;

    // A short tap is a dot; a longer hold is a dash.
    char mark = heldMs < DOT_DASH_SPLIT_MS ? '.' : '-';

    outgoingMarks += mark;

    if (outgoingMarks.length() > 6) {
      outgoingMarks = "";
    }

    sendToken(mark);
    flashTransmit();

    lastMarkAt = millis();
    statusText = (mark == '.') ? "TX DOT" : "TX DASH";

    drawUi();
  }

  keyWasDown = keyDown;

  // A pause after the last mark completes one Morse character.
  if (!keyDown && outgoingMarks.length() &&
      millis() - lastMarkAt >= CHARACTER_PAUSE_MS) {
    finishOutgoingCharacter();
  }
}

void checkControl() {
  bool controlDown = digitalRead(CONTROL_PIN) == LOW;

  // A quick press clears local text and marks.
  if (controlDown && !controlWasDown) {
    receivedText = "";
    receivedMarks = "";
    outgoingMarks = "";

    statusText = "SCREEN CLEARED";
    drawUi();
  }

  controlWasDown = controlDown;
}

// ---------- Startup ----------
void setup() {
  pinMode(KEY_PIN, INPUT_PULLUP);
  pinMode(CONTROL_PIN, INPUT_PULLUP);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TX_LED_PIN, OUTPUT);
  pinMode(RX_LED_PIN, OUTPUT);

  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(TX_LED_PIN, LOW);
  digitalWrite(RX_LED_PIN, LOW);

  // OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin();
  drawUi();

  // SX1278 LoRa module
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);

  radioReady = LoRa.begin(LORA_FREQUENCY);
  statusText = radioReady ? "RADIO ONLINE" : "RADIO ERROR";

  drawUi();
}

void loop() {
  checkRadio();
  checkKey();
  checkControl();
  updateOutputs();
}
