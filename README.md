# LORA-CW

A standalone Morse code (CW) key/receiver for ESP32, using a LoRa radio for the RF link and an SH1106 128×64 OLED for the UI. Supports over-the-air (OTA) firmware updates over WiFi once initial upload is done via USB.

## Features

- Morse key input with automatic dot/dash timing and letter/word segmentation
- LoRa transmission and reception of Morse tokens (`.`, `-`, `/`, ` `) using a simple one-byte-per-packet protocol
- SH1106 OLED UI showing live TX/RX cards, a timing progress bar, a scrolling received-message log, RSSI signal bars, WiFi status, and a status/footer line
- Local sidetone buzzer feedback on key-down and on received marks
- TX/RX LED indicators (local transmission/reception only — not delivery acknowledgment)
- Background WiFi connection with automatic retry, and ArduinoOTA support for wireless firmware updates
- Partial-display updates (dirty-tile diffing) to keep the I2C bus and UI responsive without blocking key/radio timing

## Hardware

| Component | Notes |
|---|---|
| ESP32 dev board | Any standard ESP32 devkit |
| SX1278 LoRa module | 433 MHz variant expected (`LORA_FREQUENCY = 433000000L`) |
| SH1106 128×64 OLED | I2C, hardware I2C driver (`U8G2_SH1106_128X64_NONAME_F_HW_I2C`) |
| Morse key (paddle/switch) | Wired to `KEY_PIN`, active-low |
| Control button | Wired to `CONTROL_PIN`, active-low |
| Buzzer | Active buzzer expected; see note below for passive piezos |
| TX / RX LEDs | Simple indicator LEDs |

### Pinout (default)

| Signal | GPIO |
|---|---|
| LoRa SCK | 18 |
| LoRa MISO | 19 |
| LoRa MOSI | 23 |
| LoRa CS (NSS) | 16 |
| LoRa RST | 26 |
| LoRa DIO0 | 25 |
| OLED SDA | 21 |
| OLED SCL | 22 |
| Buzzer | 33 |
| TX LED | 32 |
| RX LED | 13 |
| Key input | 27 |
| Control button | 14 |

All pins are defined as `constexpr` near the top of the sketch and can be changed there if your wiring differs.

## Libraries

- [LoRa by Sandeep Mistry](https://github.com/sandeepmistry/arduino-LoRa) — `^0.8.0`
- [U8g2 by olikraus](https://github.com/olikraus/u8g2) — `^2.35.19`
- ESP32 Arduino core's built-in `WiFi` and `ArduinoOTA` libraries

These are already declared in `platformio.ini` under `lib_deps`.

## Building and flashing (PlatformIO)

This project ships with two environments:

- **`env:esp32dev`** — standard USB/serial upload, used for the first flash
- **`env:ota`** — wireless upload over WiFi via `espota`, extends `env:esp32dev`

### First flash (USB)

Before building, fill in your credentials in the sketch:

```cpp
const char* WIFI_SSID = "your-ssid";
const char* WIFI_PASS = "your-password";
const char* OTA_HOSTNAME = "lora-cw";
const char* OTA_PASSWORD = "";   // optional
```

Then build and upload over USB:

```sh
pio run -e esp32dev -t upload
pio device monitor -b 115200
```

> The board must use an **OTA-capable partition scheme** (e.g. "Minimal SPIFFS" or "Default with OTA") so `ArduinoOTA` has two app partitions to swap between.

### Subsequent updates (OTA / WiFi)

Once the device has joined your WiFi network (check the serial monitor or the OLED footer for its IP address), edit `platformio.ini` and set the device's IP as the upload port:

```ini
[env:ota]
extends = env:esp32dev
upload_protocol = espota
upload_port = 192.168.1.42   ; replace with your device's IP
upload_flags =
    --progress
```

Then upload wirelessly:

```sh
pio run -e ota -t upload
```

While an OTA update is in progress, the device suspends normal key/radio operation, silences all outputs, and shows a progress screen on the OLED. **Keep the device powered during the update.**

## Controls

| Action | Result |
|---|---|
| Tap **KEY**, release under 250 ms | Sends a dot |
| Tap **KEY**, hold 250 ms or more | Sends a dash |
| Pause 850 ms after the last mark | Ends the current letter (auto-sent) |
| Tap **CONTROL** (release before 700 ms) | Clears local input/display buffers |
| Hold **CONTROL** for 700 ms | Sends a word space |

Clearing the buffers only affects local/pending state — any letter already transmitted over LoRa cannot be recalled.

## Display layout

- **Header** — title, LoRa status ("433" or "RF!"), RSSI bars + value (or "RX --" if nothing received yet), WiFi status bars/X
- **TX card** — current outgoing dot/dash pattern, decoded letter preview, lit while keying
- **RX card** — current incoming dot/dash pattern, last decoded letter, lit briefly on reception
- **Progress bar** — fills to show elapsed time toward the current threshold (dot/dash split, control hold, or letter-pause timeout)
- **Log** — last two lines (up to 48 characters) of the received message
- **Footer** — rotates between radio/WiFi status, transient status messages, TX queue size, and the device's IP address

## Protocol notes

- Each Morse element is sent as a single raw byte over LoRa: `.`, `-`, `/` (end of letter), or ` ` (word space).
- LoRa radio defaults (including **CRC off**) are intentionally preserved to remain compatible with the original peer device — do not enable CRC without also updating the receiving end.
- TX/RX LED indicators reflect **local** transmit/receive activity only; there is no delivery acknowledgment from the remote peer.

## Configuration reference

Key tunables are defined as `constexpr` values near the top of the sketch:

| Constant | Purpose |
|---|---|
| `LORA_FREQUENCY` | LoRa carrier frequency (Hz) |
| `OLED_I2C_HZ` | OLED I2C bus speed; lower to `100000` if your module/wiring needs it |
| `KEY_SIDETONE` | Set `false` to disable the local sidetone (receive-only audio) |
| `DOT_DASH_SPLIT_MS` | Threshold between a dot and a dash |
| `CHARACTER_PAUSE_MS` | Gap that ends a letter |
| `CONTROL_HOLD_MS` | Hold duration on CONTROL to send a word space |
| `TX_TIMEOUT_MS` | Increase if using very slow LoRa settings |
| `RX_STALE_MS` | Time before an incomplete received letter is discarded |
| `WIFI_CONNECT_TIMEOUT_MS` / `WIFI_RETRY_INTERVAL_MS` | WiFi connection attempt/retry timing |

## Hardware notes

- The buzzer is assumed to be **active** (driven simply HIGH/LOW). If you use a **passive piezo**, you'll need to drive `BUZZER_PIN` with PWM/tone output instead of a digital HIGH/LOW.
- Radio TX and WiFi connection waits are cooperative (non-blocking in the main loop), but LoRa SPI transfers, radio initialization, and the actual OTA flash write are synchronous and will briefly block.

## Troubleshooting

- **"RADIO OFFLINE" / "RF!" in header** — check LoRa module wiring (SPI pins, CS/RST/DIO0) and frequency match with the peer.
- **OLED shows nothing** — verify I2C wiring and try lowering `OLED_I2C_HZ` to `100000`.
- **WiFi never connects** — Morse/LoRa functionality still works without WiFi; OTA simply won't be available until the device joins the network. Check credentials and signal strength.
- **OTA upload fails** — confirm the board's partition scheme supports OTA, that the device is on the same network, and that `upload_port` in `platformio.ini` matches the device's current IP.
