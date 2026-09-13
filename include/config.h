#pragma once

#include <stdint.h>

namespace pins {
constexpr int LORA_SCK = 18;
constexpr int LORA_MISO = 19;
constexpr int LORA_MOSI = 23;
constexpr int LORA_CS = 16;
constexpr int LORA_RST = 26;
constexpr int LORA_DIO0 = 25;

constexpr int OLED_SDA = 21;
constexpr int OLED_SCL = 22;

constexpr int BUZZER = 33;
constexpr int TX_LED = 32;
constexpr int RX_LED = 13;

constexpr int KEY_INPUT = 27;
constexpr int CONTROL_INPUT = 14;
}  // namespace pins

namespace radio_cfg {
constexpr long FREQUENCY_HZ = 433000000L;
constexpr int TX_POWER_DBM = 17;
constexpr int SPREADING_FACTOR = 9;
constexpr long BANDWIDTH_HZ = 125000;
constexpr int CODING_RATE_DENOM = 5;
constexpr uint8_t SYNC_WORD = 0xF3;
constexpr int PREAMBLE_LENGTH = 8;
constexpr bool ENABLE_CRC = true;
}  // namespace radio_cfg

namespace timing {
constexpr uint32_t DOT_DASH_SPLIT_MS = 250;
constexpr uint32_t CHARACTER_PAUSE_MS = 850;
constexpr uint32_t DEBOUNCE_MS = 8;
constexpr uint32_t CONTROL_HOLD_MS = 700;
constexpr uint32_t RX_STALE_MS = 10000;
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
constexpr bool KEY_SIDETONE = true;
}  // namespace timing

namespace link_cfg {
constexpr uint32_t ACK_TIMEOUT_MS = 1200;
constexpr uint8_t MAX_RETRIES = 4;
constexpr uint32_t RETRY_BACKOFF_MS = 150;
constexpr uint32_t TX_START_TIMEOUT_MS = 4000;
}  // namespace link_cfg

namespace display_cfg {
constexpr uint32_t I2C_HZ = 400000;
constexpr uint8_t SCREEN_W = 128;
constexpr uint8_t SCREEN_H = 64;
}  // namespace display_cfg

namespace app_cfg {
constexpr uint8_t MAX_MARKS = 6;
constexpr uint8_t LOG_COLS = 24;
constexpr uint8_t LOG_CAPACITY = 2 * LOG_COLS;
constexpr uint8_t TX_QUEUE_SIZE = 32;
constexpr uint8_t BEEP_QUEUE_SIZE = 16;
constexpr const char* OTA_HOSTNAME = "lora-cw";
}  // namespace app_cfg
