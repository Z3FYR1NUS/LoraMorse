# LORA-CWA Dual-Purpose ESP32 Firmware

A **Morse-code keyer** that transmits dot/dash tokens over LoRa, paired with a **128×64 SH1106 OLED UI** and optional **WiFi/OTA** for wireless firmware updates. Every over-the-air token is authenticated with AES-128 using a fresh hardware-TRNG nonce per packet.

---

## Features

- **Live Morse Keying:** Tactile push-button input with automatic dot/dash discrimination via hold duration.
- **LoRa Transport @ 433 MHz:** Configurable RF parameters using `sandeepmistry/LoRa`.
- **Per-Packet AES-128 Authentication:** Random 8-byte nonce per packet with a keystream-derived tag to reject forged or corrupted frames.
- **SSD1306/SH1106 OLED UI:** Displays headers, live keying preview, animated RX "listening" bars, sliding message log, and a status footer.
- **Dirty-Tile Display Flushing:** Pushes only changed 8-byte tiles over I²C in small batches between RF/key servicing.
- **Non-Blocking State Machines:** Cooperative scheduling in `loop()` for TX queue, RX FIFO drain, beep queue, WiFi retry, and UI animations.
- **WiFi + ArduinoOTA:** Opt-in remote updates; safely suspends radio, queues a boundary token on failure, and draws a full-screen progress UI.

---

## Hardware

| Signal | GPIO | Notes |
|---|---|---|
| LoRa SCK / MISO / MOSI | 18 / 19 / 23 | VSPI |
| LoRa CS / RST / DIO0 | 16 / 26 / 25 | Standard SPI control |
| OLED SDA / SCL | 21 / 22 | I²C, 400 kHz |
| Buzzer | 33 | Sidetone + RX beeps |
| TX LED / RX LED | 32 / 13 | Active-high |
| KEY (straight key) | 27 | `INPUT_PULLUP`, active-low |
| CONTROL (function) | 14 | `INPUT_PULLUP`, active-low |

**Display:** U8g2 `SH1106 128×64` (Full framebuffer, HW I²C, noname).

---

## Configuration

All core configurations reside at the top of `src/main.cpp`.

### Required — AES Key

```cpp
constexpr char AES_KEY_HEX[] = "";   // 32 hex chars = 16 bytes
```

Generate a valid key using OpenSSL:

```bash
openssl rand -hex 16
```

> **Note:** The firmware halts in `setup()` if `AES_KEY_HEX` is not exactly 32 hex characters. Both ends of the radio link must use identical keys.

### Optional — WiFi / OTA

```cpp
const char* WIFI_SSID    = "";
const char* WIFI_PASS    = "";
const char* OTA_HOSTNAME = "";
const char* OTA_PASSWORD = "";   // Leave empty to skip OTA authentication
```

If `WIFI_SSID` is left empty, the WiFi radio is set to `WIFI_OFF` and Morse/LoRa keying operates standalone.

### Radio Parameters

```cpp
constexpr long LORA_FREQUENCY = 433000000L;   // 433 MHz
```

The header and splash screen automatically derive the `433`, `868`, or `915` label from this constant.

---

## Build & Flash (PlatformIO)

### Dependencies

```ini
sandeepmistry/LoRa @ ^0.8.0
olikraus/U8g2      @ ^2.35.19
```

* Platform: `espressif32`
* Board: `esp32dev`
* Framework: `arduino`

### Standard USB Upload

```bash
pio run -e esp32dev -t upload
pio device monitor -b 115200
```

Configuration defaults: `upload_speed = 921600`, `CORE_DEBUG_LEVEL = 0`.

### OTA Upload

Configure `upload_port` in `platformio.ini` with the target IP, then execute:

```bash
pio run -e ota -t upload
```

The `--auth=<password>` must match `OTA_PASSWORD`. Progress renders on the OLED during transfer; keying and RF tasks are paused for the duration.

---

## Controls

| Input | Action |
|---|---|
| `KEY` tap (< 250 ms) | Send dot (`.`) |
| `KEY` hold (≥ 250 ms) | Send dash (`-`) |
| `KEY` idle ≥ 850 ms | Finalize letter and queue `/` boundary |
| `CONTROL` tap | Clear buffers (locally + notify peer) |
| `CONTROL` hold ≥ 700 ms | Queue a space token (` `) |

---

## Protocol

Every LoRa packet is `ENC_PACKET_LEN = 10` bytes:

```text
[ nonce (8) ][ ciphertext (1) ][ tag (1) ]
```

1. **Nonce:** 8 random bytes generated via `esp_random()` (hardware TRNG).
2. **Keystream:** Derived via `AES-128-ECB(nonce || 0x00 * 8)` -> 16-byte block.
3. **Ciphertext:** `Byte 0 of plaintext = token ^ keystream[0]`.
4. **Tag:** `keystream[1]`.
5. **Tokens:** ASCII characters (`.`, `-`, `/`, or `' '`).

Because each nonce is fresh, keystream bytes are never reused. Non-`ENC_PACKET_LEN` packets are dropped and logged as `BAD PACKET SIZE`. Tag mismatches trigger a `BAD/UNAUTH PACKET` warning.

---

## UI Layout (128 × 64)

```text
┌────────────────────────────────────────────────┐
│ LORA-CW  433  ▂▄▆█ -72      W  ▂▄▆█  │ Header
├────────────────────────────────────────────────┤
│ [TX] ··-   A   │  [RX] ·-     N       │ Key preview
├────────────────────────────────────────────────┤
│ ████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │ Progress
├────────────────────────────────────────────────┤
│ HELLO WORLD...                                 │
│ ...MESSAGE LOG (24 × 3)                        │ Message log
├────────────────────────────────────────────────┤
│           CTRL: TAP CLEAR / HOLD SPACE        │ Footer
└────────────────────────────────────────────────┘
```

The footer alternates between status and IP every 5 seconds, reflecting live keying state when active.

---

## Architecture Notes

### Cooperative Scheduling
`loop()` shares a single `millis()` timestamp across all subsystems:

```cpp
serviceInputs(now);   // Debounced buttons, key timing, finalization
serviceRadio(now);    // TX queue drain + RX parsing
serviceOutputs(now);  // LED pulses, beep queue, sidetone
serviceTimers(now);   // Status expiry, cursor blink, RX timeout
maintainWiFi(now);    // Retry state machine (250 ms poll)
serviceDisplay(now);  // UI draw + single dirty-tile batch flush
```

No blocking `delay()` calls are present in the main execution path.

### Display Flushing
`drawUi()` renders the full frame into U8g2’s buffer. `flushDisplayStep()` pushes one row-adjacent run of changed tiles (up to 4) per loop iteration, returning immediately to preserve RF and key sampling latency.

### RX Stale Timeout
If the receiver remains mid-character (`incoming.length > 0` or `rxDiscarding`) for >= 10 s without receiving a mark, state clears and logs `RX GAP / LOST END`.

### OTA Safety
- `onStart`: Drains TX queue, stops outputs, idles LoRa, and renders progress UI.
- `onError`: Restarts input debouncers, re-queues a `/` token to reset peer decoder, and reports error code.
- `onEnd`: Retains `otaActive = true` through reboot.

---

## Serial Diagnostics (115200 Baud)

| Tag | Meaning |
|---|---|
| `[FATAL]` | AES key malformed — firmware halted in `setup()`. |
| `[LoRa]` | Radio initialization state and parameters. |
| `[WiFi]` | Connection attempts, IP acquisition, and link state. |
| `[OTA]` | Flash progress, completion, and error codes. |

---

## Troubleshooting

| Symptom | Likely Cause |
|---|---|
| Firmware halts, logs `[FATAL]` | `AES_KEY_HEX` is not 32 hex characters or contains invalid characters. |
| Header displays `RF!` | `LoRa.begin()` failed — verify wiring, CS/RST/DIO0 pins, and 3.3V rail. |
| `BAD/UNAUTH PACKET` on RX | Key mismatch between nodes or severe RF corruption. |
| `BAD PACKET SIZE` | Peer firmware version mismatch or stray packet on frequency. |
| WiFi icon shows `✗` | `WIFI_SSID` is empty or credentials are incorrect. |
| OLED display lag | `OLED_I2C_HZ` set too high for hardware module; drop to `200000`. |
| No RX frames received | Receiver uninitialized or peer transmitter out of range. |

---

## License

```text
MIT License

Copyright (c) 2026 LORA-CW contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
