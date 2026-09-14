# LORA-CW

Encrypted, ARQ-reliable Morse (CW) transceiver for ESP32 + SX127x LoRa radios. Live telemetry on an SH1106 OLED, hardware sidetone, WiFi OTA updates.

## Features

- Real-time Morse timing discrimination (dot / dash / character gap)
- Outgoing character batching into fixed-size payloads
- AES-128-GCM authenticated encryption (random 12-byte nonce, 16-byte tag)
- Stop-and-wait ARQ — sequence numbers, ACKs, retransmission, duplicate suppression
- Sequence numbers persisted across reboots (ESP32 NVS)
- Live telemetry on a 128×64 SH1106 OLED — RSSI, outbox state, sequence counters, message log
- Buzzer sidetone, dedicated TX/RX LEDs
- WiFi OTA firmware updates (ArduinoOTA)

## Hardware

| Component | GPIO | Notes |
|---|---:|---|
| LoRa SCK | 18 | SPI clock |
| LoRa MISO | 19 | SPI MISO |
| LoRa MOSI | 23 | SPI MOSI |
| LoRa CS | 16 | Chip select |
| LoRa RST | 26 | Reset |
| LoRa DIO0 | 25 | TX/RX IRQ |
| OLED SDA | 21 | I2C data (SH1106, 400 kHz) |
| OLED SCL | 22 | I2C clock |
| Morse key | 27 | `INPUT_PULLUP`, active low |
| Control button | 14 | `INPUT_PULLUP`, active low |
| Buzzer | 33 | Active high |
| TX LED | 32 | Active high |
| RX LED | 13 | Active high |

## Build & Flash (PlatformIO)

```ini
[env]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
lib_deps =
    sandeepmistry/LoRa @ ^0.8.0
    olikraus/U8g2 @ ^2.35.19

[env:esp32dev]
upload_speed = 921600
build_flags = -D CORE_DEBUG_LEVEL=0

[env:ota]
extends = env:esp32dev
upload_protocol = espota
upload_port =
upload_flags = --progress --auth=
```

```sh
pio run -e esp32dev -t upload   # initial flash over USB
pio run -e ota -t upload        # later updates over WiFi (needs ENABLE_OTA = true)
```

Requires an NVS-capable partition scheme (e.g. "Default 4MB with spiffs") for the persistent sequence counter.

## Configuration

Edit the `USER CONFIGURATION` block per node before flashing:

```cpp
// Device addressing — unique per node, mirrored on the peer
constexpr uint16_t DEVICE_ID = 0x0002;
constexpr uint16_t PEER_DEVICE_ID = 0x0001;

// AES-128 key — 32 hex chars, identical on both nodes
constexpr char AES_KEY_HEX[] = "00112233445566778899AABBCCDDEEFF";

// LoRa RF parameters — must match on both nodes
constexpr long LORA_FREQUENCY = 433000000L;   // check local regulations
constexpr long LORA_BANDWIDTH = 125000L;
constexpr int  LORA_SPREADING_FACTOR = 7;
constexpr int  LORA_CODING_RATE = 5;
constexpr uint8_t LORA_SYNC_WORD = 0x12;

// WiFi / OTA
const char* WIFI_SSID = "YourSSID";
const char* WIFI_PASS = "YourPassword";
constexpr bool ENABLE_OTA = true;
```

Two-node example:

| | `DEVICE_ID` | `PEER_DEVICE_ID` |
|---|---:|---:|
| Node A | `0x0001` | `0x0002` |
| Node B | `0x0002` | `0x0001` |

> Don't commit production keys or WiFi credentials.

## Morse Engine

| Threshold | Value |
|---|---:|
| Dot / dash split | `< 220 ms` / `>= 220 ms` |
| Character timeout | `600 ms` |
| Outbox idle flush | `1500 ms` |
| Max payload before flush | `16 bytes` |
| Control-button long press | `>= 500 ms` |

Short tap on the control button flushes the outbox and transmits immediately. Long press appends a word space, then flushes.

## Protocol

Frame layout: `header (24B) + ciphertext (0–16B) + auth tag (16B)`, max **56 bytes**.

| Offset | Field | Type | Description |
|---|---|---|---|
| `0x00` | Magic | `uint8_t` | `0xC7` |
| `0x01` | Version | `uint8_t` | `0x01` |
| `0x02` | Type | `uint8_t` | `1` = data, `2` = ACK |
| `0x03–04` | Source ID | `uint16_t` (BE) | |
| `0x05–06` | Dest ID | `uint16_t` (BE) | |
| `0x07–0A` | Sequence | `uint32_t` (BE) | Monotonic, persisted in NVS |
| `0x0B–16` | Nonce | `uint8_t[12]` | From `esp_random()` |
| `0x17` | Payload length | `uint8_t` | `0–16` |

Encryption: AES-128-GCM, 12-byte random nonce, 16-byte tag — forged or corrupted frames are rejected.

### Reliability (stop-and-wait ARQ)

```
Sender                  Receiver
  │── DATA seq=N ──────>│
  │<──── ACK seq=N ──────│
```

- ACK timeout `400 ms`, up to `3` retransmissions per frame
- Receiver tracks seen sequence numbers, suppresses duplicate delivery, still ACKs them

## OLED UI

SH1106 128×64, I2C @ 400 kHz. Shows TX/RX state, RSSI, outbox contents, sequence counters, message history.

## Dependencies

| Library | Purpose |
|---|---|
| [LoRa](https://github.com/sandeepmistry/arduino-LoRa) (sandeepmistry) | SX127x driver |
| [U8g2](https://github.com/olikraus/u8g2) (olikraus) | OLED driver |
| Preferences, ArduinoOTA, WiFi, mbedTLS | Bundled with the ESP32 Arduino core |

## Deployment Checklist

- [ ] Wiring matches the pinout table (LoRa, OLED, key, button, buzzer, LEDs)
- [ ] `DEVICE_ID` unique per node; `PEER_DEVICE_ID` points at the correct peer
- [ ] Same AES-128 key on both nodes
- [ ] Same LoRa RF parameters on both nodes; frequency legal for your region
- [ ] WiFi credentials set if `ENABLE_OTA = true`
- [ ] Partition scheme includes NVS
- [ ] No secrets committed to version control

## License

MIT
