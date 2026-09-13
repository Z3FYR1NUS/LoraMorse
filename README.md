# LORA-CW

A standalone Morse code (CW) key/receiver for ESP32, using a LoRa radio for the RF link and an SH1106 128×64 OLED for the UI. RF packets are encrypted and authenticated with AES-128. Supports over-the-air (OTA) firmware updates over WiFi once the initial upload is done via USB.

## Features

- Morse key input with automatic dot/dash timing and letter/word segmentation
- **Encrypted, authenticated LoRa link** — every packet is AES-128 protected; corrupted or forged packets are rejected before they reach the decoder
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
- ESP32 Arduino core's bundled **mbedtls** (`mbedtls/aes.h`) for AES-128, and `esp_random()` (hardware TRNG) — no extra dependency needed

These are already declared in `platformio.ini` under `lib_deps`.

## Building and flashing (PlatformIO)

This project ships with two environments:

- **`env:esp32dev`** — standard USB/serial upload, used for the first flash
- **`env:ota`** — wireless upload over WiFi via `espota`, extends `env:esp32dev`

### First flash (USB)

Before building, fill in your WiFi/OTA credentials and generate an encryption key.

**1. Generate a key on your own machine** (never paste real keys into chat, tickets, or anywhere else they could leak):

```sh
openssl rand -hex 16
```

This prints a 32-character hex string, e.g. `9f2c...` (illustrative only — generate your own).

**2. Paste it into `main.cpp`:**

```cpp
const char* WIFI_SSID = "your-ssid";
const char* WIFI_PASS = "your-password";
const char* OTA_HOSTNAME = "lora-cw";
const char* OTA_PASSWORD = "";   // optional

// Paste your own openssl rand -hex 16 output here — do not reuse this example:
constexpr char AES_KEY_HEX[] = "9f2c8a3e1d4b6f705c9a2e8d1b4f6073";
```

The firmware decodes `AES_KEY_HEX` into the raw AES key once at boot (`loadAesKeyFromHex()` in `setup()`). If the string is missing, the wrong length, or contains a non-hex character, the device halts and prints an error over serial instead of silently running with a broken key.

> Don't commit your real `AES_KEY_HEX` to a public repository. Consider moving it into a local, gitignored header, or loading it from NVS, once you're past initial bring-up.

**3. Build and upload over USB:**

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
- **Footer** — rotates between radio/WiFi status, transient status messages (including `BAD/UNAUTH PACKET` or `BAD PACKET SIZE` if a corrupted/foreign packet is rejected), TX queue size, and the device's IP address

## Security / packet encryption

Every over-the-air packet carries a single logical Morse token (`.`, `-`, `/`, or `' '`), but the on-air bytes are encrypted and authenticated:

```
[8-byte random nonce][1-byte ciphertext][1-byte authentication tag]   (10 bytes total)
```

How it works:

- A fresh, random 8-byte nonce (from the ESP32's hardware TRNG, `esp_random()`) is generated for **every** packet.
- That nonce is encrypted once with AES-128-ECB under the pre-shared key, producing 16 keystream bytes. This uses AES as a keyed PRF rather than a traditional stream-cipher mode — since the nonce never repeats, no keystream byte is ever reused, so there's no counter/IV state to keep in sync between TX and RX.
- **Ciphertext** = `token XOR keystream[0]`
- **Tag** = `keystream[1]` — the receiver independently recomputes the keystream from the received nonce and its own copy of the key; if the received tag doesn't match, the packet is dropped as corrupted or forged, without ever touching the decoder.

### Key setup

The raw 16-byte AES key is never typed in by hand. Instead:

1. Generate 32 hex characters with `openssl rand -hex 16`.
2. Paste that string into `AES_KEY_HEX` in `main.cpp`.
3. At boot, `loadAesKeyFromHex()` decodes it into the working `AES_KEY` array and halts with a serial error if the string is the wrong length or contains invalid characters.
4. Paste the **exact same** `AES_KEY_HEX` value into both the transmitting and receiving device's firmware — the link only works if both sides derive the same keystream.

Implications:

- **Not compatible with an unmodified/original peer.** Both ends of the link must run this firmware version and share the identical key.
- **Packet size increased** from 1 byte to 10 bytes, which proportionally increases per-packet LoRa airtime. This is still trivial relative to a human keying speed.
- **LoRa's CRC remains off** (matching the original defaults) — the authentication tag independently catches corruption and forgery, so a separate CRC isn't needed for this purpose.
- This provides confidentiality and per-packet authenticity, but **not replay protection** — a captured packet re-sent later would still decrypt to a valid single mark. Given that a lone dot/dash carries no exploitable state on its own, this is an accepted trade-off; a sequence number could be added if stronger guarantees against replay/injection are needed.
- **Never paste a real key into chat tools, tickets, commit messages, or anywhere else outside the device's own firmware** — treat any key that has been typed somewhere else as compromised and regenerate it.

## Protocol notes

- Each Morse element is sent as one AES-protected packet (see above) rather than a bare byte.
- TX/RX LED indicators reflect **local** transmit/receive activity only; there is no delivery acknowledgment from the remote peer.

## Configuration reference

Key tunables are defined near the top of the sketch:

| Constant | Purpose |
|---|---|
| `AES_KEY_HEX` | 32-character hex string (16 bytes) decoded into the AES key at boot; must match on both peers |
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
- Radio TX and WiFi connection waits are cooperative (non-blocking in the main loop), but LoRa SPI transfers, radio initialization, AES operations, and the actual OTA flash write are synchronous and will briefly block.

## Troubleshooting

- **Device halts at boot printing `[FATAL] AES_KEY_HEX must be exactly 32 hex characters`** — `AES_KEY_HEX` was left as the placeholder, is the wrong length, or contains a typo. Generate a fresh key with `openssl rand -hex 16` and paste the full 32-character string in.
- **"RADIO OFFLINE" / "RF!" in header** — check LoRa module wiring (SPI pins, CS/RST/DIO0) and frequency match with the peer.
- **"BAD/UNAUTH PACKET" in footer** — the peer's key doesn't match yours, or the packet was corrupted/foreign. Confirm both devices run this firmware and share the identical `AES_KEY_HEX`.
- **"BAD PACKET SIZE" in footer** — something on the same frequency sent a packet that isn't 10 bytes (e.g. an unmodified/original peer, or unrelated LoRa traffic).
- **OLED shows nothing** — verify I2C wiring and try lowering `OLED_I2C_HZ` to `100000`.
- **WiFi never connects** — Morse/LoRa functionality still works without WiFi; OTA simply won't be available until the device joins the network. Check credentials and signal strength.
- **OTA upload fails** — confirm the board's partition scheme supports OTA, that the device is on the same network, and that `upload_port` in `platformio.ini` matches the device's current IP.
