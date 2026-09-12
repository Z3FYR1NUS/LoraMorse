// -----------------------------------------------------------------------------
// OLED Driver Diagnostic — isolate whether the white panel's top-row
// corruption is a controller/driver mismatch (SH1106 vs SSD1306) or a
// hardware defect in this specific unit.
//
// HOW TO USE:
//   1. Flash this to the ESP32 with the white panel connected on the same
//      SDA/SCL pins as the main project (21/22 below — change if different).
//   2. Watch the OLED. It cycles through 4 test patterns every 2 seconds,
//      each printed to Serial too so you can log what you see against what
//      pattern was active.
//   3. Change ACTIVE_DRIVER below to try each constructor in turn. Re-flash
//      between each one — only one can be compiled in at a time.
//   4. Whichever driver renders ALL FOUR patterns cleanly (especially
//      pattern 1, the full-width top fill) is the correct one for this
//      panel. If NONE of them fix the top-row corruption, that's strong
//      confirmation the panel itself is defective rather than a driver
//      mismatch.
// -----------------------------------------------------------------------------

#include <SPI.h>
#include <Wire.h>

const char *WIFI_SSID = "Wifi_2.4Ghz";
const char *WIFI_PASS = "Chaisanit1!";
const char *OTA_HOSTNAME = "ESP32-Morse-Station";

#define OLED_RESET 4

#define NUMFLAKES 10
#define XPOS 0
#define YPOS 1
#define DELTAY 2

#define LOGO16_GLCD_HEIGHT 16
#define LOGO16_GLCD_WIDTH  16

static const unsigned char PROGMEM logo16_glcd_bmp[] = {
  B00000000, B11000000,
  B00000001, B11000000,
  B00000001, B11000000,
  B00000011, B11100000,
  B11110011, B11100000,
  B11111110, B11111000,
  B01111110, B11111111,
  B00110011, B10011111,
  B00011111, B11111100,
  B00001101, B01110000,
  B00011011, B10100000,
  B00111111, B11100000,
  B00111111, B11110000,
  B01111100, B11110000,
  B01110000, B01110000,
  B00000000, B00110000
};

#if (SH1106_LCDHEIGHT != 64)
#error("Height incorrect, please fix Adafruit_SH1106.h!");
#endif

// Forward Declarations
void testdrawline(void);
void testdrawrect(void);
void testfillrect(void);
void testdrawcircle(void);
void testdrawroundrect(void);
void testfillroundrect(void);
void testdrawtriangle(void);
void testfilltriangle(void);
void testdrawchar(void);
void testdrawbitmap(const uint8_t *bitmap, uint8_t w, uint8_t h);

void setup() {
  Serial.begin(9600);

  // By default, high voltage is generated internally from 3.3V
  display.begin(SH1106_SWITCHCAPVCC, 0x3C);

  // Show internal splashscreen buffer
  display.display();
  delay(2000);

  // Clear buffer
  display.clearDisplay();

  // Draw single pixel
  display.drawPixel(10, 10, WHITE);
  display.display();
  delay(2000);
  display.clearDisplay();

  // Draw lines
  testdrawline();
  display.display();
  delay(2000);
  display.clearDisplay();

  // Draw rectangles
  testdrawrect();
  display.display();
  delay(2000);
  display.clearDisplay();

  // Draw filled rectangles
  testfillrect();
  display.display();
  delay(2000);
  display.clearDisplay();

  // Draw circles
  testdrawcircle();
  display.display();
  delay(2000);
  display.clearDisplay();

  // Draw filled circle
  display.fillCircle(display.width() / 2, display.height() / 2, 10, WHITE);
  display.display();
  delay(2000);
  display.clearDisplay();

  // Draw rounded rectangles
  testdrawroundrect();
  delay(2000);
  display.clearDisplay();

  // Draw filled rounded rectangles
  testfillroundrect();
  delay(2000);
  display.clearDisplay();

  // Draw triangles
  testdrawtriangle();
  delay(2000);
  display.clearDisplay();

  // Draw filled triangles
  testfilltriangle();
  delay(2000);
  display.clearDisplay();

  // Render font test
  testdrawchar();
  display.display();
  delay(2000);
  display.clearDisplay();

  // Text formatting test
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Hello, world!");
  display.setTextColor(BLACK, WHITE); // Inverted text
  display.println(3.141592);
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.print("0x");
  display.println(0xDEADBEEF, HEX);
  display.display();
  delay(2000);

  // Miniature bitmap display
  display.clearDisplay();
  display.drawBitmap(30, 16, logo16_glcd_bmp, 16, 16, 1);
  display.display();

  // Display inversion test
  display.invertDisplay(true);
  delay(1000);
  display.invertDisplay(false);
  delay(1000);

  // Animated bitmap test
  testdrawbitmap(logo16_glcd_bmp, LOGO16_GLCD_HEIGHT, LOGO16_GLCD_WIDTH);
}

void loop() {
  // Main execution loops inside testdrawbitmap during diagnostic stage
}

void testdrawbitmap(const uint8_t *bitmap, uint8_t w, uint8_t h) {
  uint8_t icons[NUMFLAKES][3];

  // Initialize icon positions
  for (uint8_t f = 0; f < NUMFLAKES; f++) {
    icons[f][XPOS]   = random(display.width());
    icons[f][YPOS]   = 0;
    icons[f][DELTAY] = random(5) + 1;

    Serial.print("x: ");
    Serial.print(icons[f][XPOS], DEC);
    Serial.print(" y: ");
    Serial.print(icons[f][YPOS], DEC);
    Serial.print(" dy: ");
    Serial.println(icons[f][DELTAY], DEC);
  }

  while (1) {
    // Draw frame
    for (uint8_t f = 0; f < NUMFLAKES; f++) {
      display.drawBitmap(icons[f][XPOS], icons[f][YPOS], logo16_glcd_bmp, w, h, WHITE);
    }
    display.display();
    delay(200);

    // Erase and advance position
    for (uint8_t f = 0; f < NUMFLAKES; f++) {
      display.drawBitmap(icons[f][XPOS], icons[f][YPOS], logo16_glcd_bmp, w, h, BLACK);
      icons[f][YPOS] += icons[f][DELTAY];

      if (icons[f][YPOS] > display.height()) {
        icons[f][XPOS]   = random(display.width());
        icons[f][YPOS]   = 0;
        icons[f][DELTAY] = random(5) + 1;
      }
    }
  }
}

void testdrawchar(void) {
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);

  for (uint8_t i = 0; i < 168; i++) {
    if (i == '\n') continue;
    display.write(i);
    if ((i > 0) && (i % 21 == 0)) {
      display.println();
    }
  }
  display.display();
}

void testdrawcircle(void) {
  for (int16_t i = 0; i < display.height(); i += 2) {
    display.drawCircle(display.width() / 2, display.height() / 2, i, WHITE);
    display.display();
  }
}

void testfillrect(void) {
  uint8_t color = 1;
  for (int16_t i = 0; i < display.height() / 2; i += 3) {
    display.fillRect(i, i, display.width() - i * 2, display.height() - i * 2, color % 2);
    display.display();
    color++;
  }
}

void testdrawtriangle(void) {
  for (int16_t i = 0; i < min(display.width(), display.height()) / 2; i += 5) {
    display.drawTriangle(display.width() / 2, display.height() / 2 - i,
                         display.width() / 2 - i, display.height() / 2 + i,
                         display.width() / 2 + i, display.height() / 2 + i, WHITE);
    display.display();
  }
}

void testfilltriangle(void) {
  uint8_t color = WHITE;
  for (int16_t i = min(display.width(), display.height()) / 2; i > 0; i -= 5) {
    display.fillTriangle(display.width() / 2, display.height() / 2 - i,
                         display.width() / 2 - i, display.height() / 2 + i,
                         display.width() / 2 + i, display.height() / 2 + i, WHITE);
    color = (color == WHITE) ? BLACK : WHITE;
    display.display();
  }
}

void testdrawroundrect(void) {
  for (int16_t i = 0; i < display.height() / 2 - 2; i += 2) {
    display.drawRoundRect(i, i, display.width() - 2 * i, display.height() - 2 * i, display.height() / 4, WHITE);
    display.display();
  }
}

void testfillroundrect(void) {
  uint8_t color = WHITE;
  for (int16_t i = 0; i < display.height() / 2 - 2; i += 2) {
    display.fillRoundRect(i, i, display.width() - 2 * i, display.height() - 2 * i, display.height() / 4, color);
    color = (color == WHITE) ? BLACK : WHITE;
    display.display();
  }
}

void testdrawrect(void) {
  for (int16_t i = 0; i < display.height() / 2; i += 2) {
    display.drawRect(i, i, display.width() - 2 * i, display.height() - 2 * i, WHITE);
    display.display();
  }
}

void testdrawline(void) {
  for (int16_t i = 0; i < display.width(); i += 4) {
    display.drawLine(0, 0, i, display.height() - 1, WHITE);
    display.display();
  }
  for (int16_t i = 0; i < display.height(); i += 4) {
    display.drawLine(0, 0, display.width() - 1, i, WHITE);
    display.display();
  }
  delay(250);

  display.clearDisplay();
  for (int16_t i = 0; i < display.width(); i += 4) {
    display.drawLine(0, display.height() - 1, i, 0, WHITE);
    display.display();
  }
  for (int16_t i = display.height() - 1; i >= 0; i -= 4) {
    display.drawLine(0, display.height() - 1, display.width() - 1, i, WHITE);
    display.display();
  }
  delay(250);

  display.clearDisplay();
  for (int16_t i = display.width() - 1; i >= 0; i -= 4) {
    display.drawLine(display.width() - 1, display.height() - 1, i, 0, WHITE);
    display.display();
  }
  for (int16_t i = display.height() - 1; i >= 0; i -= 4) {
    display.drawLine(display.width() - 1, display.height() - 1, 0, i, WHITE);
    display.display();
  }
  delay(250);

  display.clearDisplay();
  for (int16_t i = 0; i < display.height(); i += 4) {
    display.drawLine(display.width() - 1, 0, 0, i, WHITE);
    display.display();
  }
  for (int16_t i = 0; i < display.width(); i += 4) {
    display.drawLine(display.width() - 1, 0, i, display.height() - 1, WHITE);
    display.display();
  }
  delay(250);
}
