#include "Display.h"

#include <Arduino.h>
#include <string.h>
#include <stdio.h>

void Display::begin() {
    u8g2_.setBusClock(display_cfg::I2C_HZ);
    u8g2_.begin();
    u8g2_.setContrast(100);
    u8g2_.setFontMode(1);
    u8g2_.clearBuffer();
    u8g2_.setFont(u8g2_font_logisoso16_tf);
    centered("LORA-CW", 0, 28, 128);
    u8g2_.setFont(u8g2_font_5x7_tf);
    centered("STARTING RADIO", 0, 47, 128);
    u8g2_.sendBuffer();
    memcpy(previousFrame_, u8g2_.getBufferPtr(), FRAME_BYTES);
}

void Display::centered(const char* text, int x, int baseline, int width) {
    const int textWidth = u8g2_.getStrWidth(text);
    u8g2_.drawStr(x + (width > textWidth ? (width - textWidth) / 2 : 0), baseline, text);
}

void Display::drawBars(int x, int bottom, uint8_t count) {
    for (uint8_t i = 0; i < 4; ++i) {
        const uint8_t height = uint8_t(1 + i * 2);
        if (i < count) u8g2_.drawBox(x + i * 3, bottom - height + 1, 2, height);
        else u8g2_.drawPixel(x + i * 3, bottom);
    }
}

void Display::drawHeader(const UiState& s) {
    u8g2_.setFont(u8g2_font_4x6_tf);
    u8g2_.drawStr(2, 7, "LORA-CW");
    u8g2_.drawStr(38, 7, s.radioReady ? "433" : "RF!");
    if (s.hasRssi) {
        char rssi[8];
        snprintf(rssi, sizeof(rssi), "%d", s.rssi);
        // Relative strength indicator, not a calibrated link-quality estimate.
        const uint8_t bars = s.rssi >= -75 ? 4 : s.rssi >= -90 ? 3 : s.rssi >= -105 ? 2 : 1;
        drawBars(57, 7, bars);
        u8g2_.drawStr(72, 7, rssi);
    } else {
        u8g2_.drawStr(58, 7, "RX --");
    }
    u8g2_.drawStr(105, 7, "W");
    if (s.wifiConnected) drawBars(114, 7, 4);
    else if (s.wifiAttempting) drawBars(114, 7, 1 + (millis() / 250) % 4);
    else {
        u8g2_.drawLine(116, 2, 122, 7);
        u8g2_.drawLine(122, 2, 116, 7);
    }
    u8g2_.drawHLine(0, 9, display_cfg::SCREEN_W);
}

void Display::drawCard(int x, const char* label, const morse::Marks& marks, char last,
                        bool lit) {
    u8g2_.drawRFrame(x, 12, 61, 24, 2);
    if (lit) u8g2_.drawRBox(x + 3, 14, 13, 7, 1);
    u8g2_.setFont(u8g2_font_4x6_tf);
    u8g2_.setDrawColor(lit ? 0 : 1);
    u8g2_.drawStr(x + 5, 20, label);
    u8g2_.setDrawColor(1);
    if (marks.length) {
        int markX = x + 4;
        for (uint8_t i = 0; i < marks.length; ++i) {
            const uint8_t width = marks.text[i] == '-' ? 4 : 2;
            u8g2_.drawBox(markX, 29, width, 2);
            markX += width + 2;
        }
    } else {
        u8g2_.drawStr(x + 4, 31, last ? "LAST" : (label[0] == 'T' ? "READY" : "WAIT"));
    }
    const char candidate = marks.length ? morse::decode(marks) : last;
    if (candidate == ' ') {
        u8g2_.setFont(u8g2_font_5x7_tf);
        centered("SP", x + 40, 31, 18);
    } else if (candidate) {
        char text[2] = {candidate, '\0'};
        u8g2_.setFont(u8g2_font_logisoso16_tf);
        // Clip to this card's letter region, even if a replacement font is wider.
        u8g2_.setClipWindow(x + 40, 13, x + 60, 35);
        centered(text, x + 40, 33, 19);
        u8g2_.setMaxClipWindow();
    } else {
        u8g2_.drawHLine(x + 47, 27, 6);
    }
}

void Display::drawProgress(const UiState& s) {
    u8g2_.drawHLine(2, 39, 124);
    if (s.progressVisible) {
        uint32_t elapsed = s.progressElapsedMs;
        const uint32_t total = s.progressTotalMs ? s.progressTotalMs : 1;
        if (elapsed > total) elapsed = total;
        const uint8_t width = uint8_t(elapsed * 124UL / total);
        if (width) u8g2_.drawBox(2, 37, width, 2);
    }
}

void Display::drawLog(const UiState& s) {
    u8g2_.setFont(u8g2_font_5x7_tf);
    const size_t len = s.receivedLog ? strlen(s.receivedLog) : 0;
    if (!len) {
        centered("NO MESSAGE YET", 2, 47, 124);
        u8g2_.setFont(u8g2_font_4x6_tf);
        centered("KEY TO SEND", 2, 54, 124);
        return;
    }
    for (uint8_t row = 0; row < 2; ++row) {
        const size_t offset = row * app_cfg::LOG_COLS;
        if (offset >= len) break;
        size_t count = len - offset;
        if (count > app_cfg::LOG_COLS) count = app_cfg::LOG_COLS;
        char line[app_cfg::LOG_COLS + 1];
        memcpy(line, s.receivedLog + offset, count);
        line[count] = '\0';
        u8g2_.drawStr(2, 47 + row * 7, line);
    }
}

void Display::drawFooter(const UiState& s) {
    u8g2_.drawHLine(0, 56, display_cfg::SCREEN_W);
    u8g2_.setFont(u8g2_font_4x6_tf);
    centered(s.footerText ? s.footerText : "", 2, 63, 124);
}

void Display::drawUi(const UiState& s) {
    u8g2_.clearBuffer();
    u8g2_.setDrawColor(1);
    u8g2_.setFontMode(1);
    drawHeader(s);
    if (s.outgoing) drawCard(2, "TX", *s.outgoing, s.lastTxChar, s.txCardLit);
    if (s.incoming) drawCard(65, "RX", *s.incoming, s.lastRxChar, s.rxCardLit);
    drawProgress(s);
    drawLog(s);
    drawFooter(s);
}

void Display::flushTileStep() {
    const uint8_t* buffer = u8g2_.getBufferPtr();
    // One short contiguous tile run per loop; key and RF get serviced between runs.
    while (nextTile_ < TILE_COUNT) {
        const uint16_t start = nextTile_;
        if (!memcmp(buffer + start * 8, previousFrame_ + start * 8, 8)) {
            ++nextTile_;
            continue;
        }
        uint8_t count = 1;
        while (count < TILES_PER_TRANSFER && start + count < TILE_COUNT &&
               (start + count) / TILES_X == start / TILES_X &&
               memcmp(buffer + (start + count) * 8, previousFrame_ + (start + count) * 8, 8)) {
            ++count;
        }
        u8g2_.updateDisplayArea(start % TILES_X, start / TILES_X, count, 1);
        memcpy(previousFrame_ + start * 8, buffer + start * 8, count * 8);
        nextTile_ += count;
        return;
    }
}

void Display::service(uint32_t now, const UiState& state) {
    if (nextTile_ < TILE_COUNT) {
        flushTileStep();
        return;
    }
    if ((!dirty_ && !state.animated) || uint32_t(now - frameAt_) < timing::UI_FRAME_MS) return;
    drawUi(state);
    dirty_ = false;
    frameAt_ = now;
    nextTile_ = 0;
    flushTileStep();
}

void Display::showOtaScreen(const char* label, uint8_t percent) {
    u8g2_.clearBuffer();
    u8g2_.setDrawColor(1);
    u8g2_.setFont(u8g2_font_5x7_tf);
    centered(label, 0, 15, 128);
    char percentText[8];
    snprintf(percentText, sizeof(percentText), "%u%%", unsigned(percent));
    u8g2_.setFont(u8g2_font_logisoso16_tf);
    centered(percentText, 0, 39, 128);
    u8g2_.drawFrame(10, 47, 108, 7);
    const uint8_t fill = uint8_t(106UL * percent / 100);
    if (fill) u8g2_.drawBox(11, 48, fill, 5);
    u8g2_.setFont(u8g2_font_4x6_tf);
    centered("KEEP POWER ON", 0, 63, 128);
    // Full frames are appropriate here: normal key/radio work is suspended.
    u8g2_.sendBuffer();
    memcpy(previousFrame_, u8g2_.getBufferPtr(), FRAME_BYTES);
    nextTile_ = TILE_COUNT;
    frameAt_ = millis();
}
