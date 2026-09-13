#pragma once
// Display -- SH1106 128x64 UI with dirty-tile partial updates.
//
// The original sketch's partial-update (dirty-tile diffing) design was
// already good and is preserved as-is. What changed is that Display no
// longer reaches into App/Button/queue globals directly -- App fills in a
// plain UiState snapshot each frame, which keeps Display unit-independent
// from the rest of the app (and is what makes it possible to swap the radio
// or protocol layers without touching a single drawing routine).

#include <U8g2lib.h>
#include <stdint.h>

#include "config.h"
#include "morse/Morse.h"

// Everything Display needs to draw one frame. App fills this in from its own
// state each loop iteration; Display treats it as read-only input.
struct UiState {
    bool radioReady = false;
    bool hasRssi = false;
    int rssi = 0;
    float snr = 0.0f;

    bool wifiConnected = false;
    bool wifiAttempting = false;
    const char* ipText = nullptr;

    const morse::Marks* outgoing = nullptr;
    const morse::Marks* incoming = nullptr;
    char lastTxChar = '\0';
    char lastRxChar = '\0';
    bool txCardLit = false;
    bool rxCardLit = false;

    // Progress bar: elapsed/total counts up to whichever timeout is active.
    bool progressVisible = false;
    uint32_t progressElapsedMs = 0;
    uint32_t progressTotalMs = 1;

    const char* receivedLog = nullptr;  // up to app_cfg::LOG_CAPACITY chars

    const char* footerText = nullptr;  // fully composed by App (status/queue/IP rotation)

    bool animated = false;  // true while something is actively moving (keys down, TX pending, etc.)
};

class Display {
public:
    void begin();

    // Call every loop iteration. Internally rate-limited to timing::UI_FRAME_MS
    // and continues any in-progress tile flush before considering a new frame,
    // exactly as the original implementation did.
    void service(uint32_t now, const UiState& state);

    // Full-frame OTA progress screen (normal operation is suspended during
    // OTA, so a full redraw here is fine).
    void showOtaScreen(const char* label, uint8_t percent);

    void markDirty() { dirty_ = true; }

private:
    static constexpr uint8_t TILES_X = display_cfg::SCREEN_W / 8;
    static constexpr uint8_t TILES_Y = display_cfg::SCREEN_H / 8;
    static constexpr uint16_t TILE_COUNT = TILES_X * TILES_Y;
    static constexpr uint8_t TILES_PER_TRANSFER = 4;
    static constexpr size_t FRAME_BYTES = display_cfg::SCREEN_W * display_cfg::SCREEN_H / 8;

    U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2_{U8G2_R0, U8X8_PIN_NONE, pins::OLED_SCL,
                                              pins::OLED_SDA};
    uint8_t previousFrame_[FRAME_BYTES] = {};
    uint16_t nextTile_ = TILE_COUNT;
    uint32_t frameAt_ = 0;
    bool dirty_ = true;

    void centered(const char* text, int x, int baseline, int width);
    void drawBars(int x, int bottom, uint8_t count);
    void drawHeader(const UiState& s);
    void drawCard(int x, const char* label, const morse::Marks& marks, char last, bool lit);
    void drawProgress(const UiState& s);
    void drawLog(const UiState& s);
    void drawFooter(const UiState& s);
    void drawUi(const UiState& s);
    void flushTileStep();
};
