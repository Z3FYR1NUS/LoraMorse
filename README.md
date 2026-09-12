# LoRaMorse

Two identical ESP32 stations that exchange Morse-code dots and dashes over **433 MHz LoRa**.

Each station uses:

- ESP32 Dev Module
- SX1278 / Ra-02 433 MHz LoRa module
- 1.3-inch 128×64 SH1106 I2C OLED
- Morse key (send button)
- Clear button
- Active buzzer
- Green TX LED
- Red RX LED

Flash the **same firmware** onto both ESP32 boards.

---

## Features

- Single-key Morse input (short press = dot, long press = dash)
- Automatic character decoding after a short pause
- Real-time transmission of individual Morse tokens over LoRa
- Green LED + activity animation on transmit
- Red LED + buzzer feedback on receive
- OLED shows live TX/RX state, decoded text, RSSI, and radio status
- Clear button resets local buffers
- Optional Wi-Fi + ArduinoOTA for wireless firmware updates

---

## Parts (two stations)

| Part                                | Qty |
| ----------------------------------- | --: |
| ESP32 Dev Module                    |   2 |
| SX1278 / Ra-02 LoRa module (433 MHz)|   2 |
| SH1106 1.3" I2C OLED (128×64)       |   2 |
| 3.3 V active buzzer                 |   2 |
| Green LED                           |   2 |
| Red LED                             |   2 |
| 220 Ω resistor                      |   4 |
| Momentary push button               |   4 |
| 433 MHz LoRa antenna                |   2 |
| Breadboards + jumper wires          | as needed |

> **Important:** Power the SX1278 and the OLED from **3.3 V only**. Never connect them to 5 V.

---

## Wiring (identical on both stations)

### SH1106 OLED

| OLED | ESP32  | Notes      |
| ---- | ------ | ---------- |
| VCC  | 3V3    | Power      |
| GND  | GND    | Ground     |
| SDA  | GPIO21 | I2C data   |
| SCL  | GPIO22 | I2C clock  |

### SX1278 / Ra-02 LoRa

| LoRa     | ESP32  | Notes                |
| -------- | ------ | -------------------- |
| VCC      | 3V3    | Power                |
| GND      | GND    | Ground               |
| SCK      | GPIO18 | SPI clock            |
| MISO     | GPIO19 | SPI data from radio  |
| MOSI     | GPIO23 | SPI data to radio    |
| NSS / CS | GPIO16 | Chip select          |
| RESET    | GPIO26 | Radio reset          |
| DIO0     | GPIO25 | Radio interrupt      |

**Always attach a suitable 433 MHz antenna before powering or transmitting.**

### Buttons (active-low, internal pull-up)

| Button           | GPIO   | Other side | Purpose          |
| ---------------- | ------ | ---------- | ---------------- |
| Morse key / Send | GPIO27 | GND        | Dot / dash input |
| Clear            | GPIO14 | GND        | Reset buffers    |

### LEDs (each with its own 220 Ω resistor)

| LED      | Wiring                          |
| -------- | ------------------------------- |
| Green TX | GPIO32 → 220 Ω → LED anode → GND |
| Red RX   | GPIO13 → 220 Ω → LED anode → GND |

### Active buzzer

| Buzzer          | ESP32  |
| --------------- | ------ |
| Positive / SIG  | GPIO33 |
| Negative / GND  | GND    |

---

## GPIO summary

| Function      | GPIO |
| ------------- | ---: |
| LoRa SCK      |   18 |
| LoRa MISO     |   19 |
| LoRa MOSI     |   23 |
| LoRa CS / NSS |   16 |
| LoRa RESET    |   26 |
| LoRa DIO0     |   25 |
| OLED SDA      |   21 |
| OLED SCL      |   22 |
| Buzzer        |   33 |
| TX LED        |   32 |
| RX LED        |   13 |
| Morse key     |   27 |
| Clear button  |   14 |

---

## Morse input

The firmware measures key-hold duration:

| Hold time          | Result |
| ------------------ | ------ |
| < 250 ms           | Dot `.` |
| ≥ 250 ms           | Dash `-` |

After the last mark, the firmware waits ~850 ms of silence and then treats the sequence as a completed character.

Examples:

```
.       → E
.-      → A
...     → S
---     → O
```

### Sending SOS

1. Tap three times → `...` → wait → **S**
2. Hold three times → `---` → wait → **O**
3. Tap three times → `...` → wait → **S**

The receiving station decodes each character and appends it to its message buffer.

---

## LoRa protocol

Both stations use:

```cpp
constexpr long LORA_FREQUENCY = 433E6;   // 433 MHz
```

Tokens sent over the air:

| Token | Meaning              |
| ----- | -------------------- |
| `.`   | Dot                  |
| `-`   | Dash                 |
| `/`   | End of character     |
| ` `   | Word space           |

The radio transmits each token immediately rather than waiting for a complete message.

---

## OLED layout

```
┌──────────────────────────────────────┐
│ LORA-CW                    WIFI OK   │
├──────────────────┬───────────────────┤
│ TX               │ RX                │
│ READY / .-       │ WAIT / A          │
├──────────────────────────────────────┤
│ RSSI -72 dBm              MSG 4/21   │
│ ████████                             │  ← activity bar
│ HELLO                                │  ← received text
├──────────────────────────────────────┤
│ KEY READY                   433 MHz  │
└──────────────────────────────────────┘
```

- **TX card** – current marks being entered or last transmitted character
- **RX card** – current marks being received or last received character
- **RSSI** – last packet signal strength (or `N/A`)
- **MSG** – characters in the rolling receive buffer
- **Footer** – status messages, key state, radio health, or IP address

---

## Wi-Fi & OTA

ArduinoOTA is supported for wireless updates after the first USB flash.

Edit these constants in `main.cpp`:

```cpp
const char* WIFI_SSID     = "your-ssid";
const char* WIFI_PASS     = "your-password";
const char* OTA_HOSTNAME  = "Morse-Station-A";   // unique per station
```

Recommended hostnames:

- `Morse-Station-A`
- `Morse-Station-B`

Never commit real credentials to the repository.

---

## PlatformIO

`platformio.ini`:

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
build_flags =
    -D CORE_DEBUG_LEVEL=0

[env:ota]
extends = env:esp32dev
upload_protocol = espota
upload_port =        # ← change to the ESP32’s IP
upload_flags =
    --progress
```

### Build & upload (USB)

```bash
pio run -e esp32dev -t upload
```

### Serial monitor

```bash
pio device monitor -b 115200
```

### OTA upload

1. Note the IP shown on the OLED footer (or Serial).
2. Set `upload_port` in `platformio.ini`.
3. Run:

```bash
pio run -e ota -t upload
```

---

## First-time setup

1. Wire both stations exactly as shown above.
2. Attach 433 MHz antennas.
3. (Optional) Fill in Wi-Fi credentials and unique OTA hostnames.
4. Connect the first ESP32 via USB and upload.
5. Repeat for the second board.
6. Test Morse transmission between the two stations.
7. Once OTA works, subsequent updates can be done wirelessly.

---

## Troubleshooting

| Symptom                  | Check                                                                 |
| ------------------------ | --------------------------------------------------------------------- |
| OLED blank               | VCC→3V3, GND, SDA→21, SCL→22, SH1106 configuration                  |
| LoRa fails to init       | 3.3 V power, shared GND, SPI pins, CS/RST/DIO0, antenna connected   |
| No messages received     | Both modules are 433 MHz, same frequency, antennas present, same firmware |
| Morse key does nothing   | GPIO27 → button → GND (active-low)                                   |
| Clear button does nothing| GPIO14 → button → GND                                                |
| OTA fails                | Same network, correct IP in `upload_port`, ArduinoOTA running        |

---

## Safety notes

- Never power the SX1278 or SH1106 from 5 V.
- Always use a 220 Ω series resistor with each LED.
- Keep the LoRa antenna connected whenever the radio is powered or transmitting.
- Use a clean, regulated 3.3 V supply for the radio and display.

---

## Power

Both stations are powered from the ESP32 USB port. Current draw varies with LoRa TX, OLED, Wi-Fi, and buzzer activity.
