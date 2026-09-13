# LORA-CW

An encrypted, reliable Morse code (CW) transceiver framework built for ESP32 microcontrollers operating over LoRa physical-layer radios (SX127x).

The system provides:

* Real-time Morse key timing analysis
* Outgoing word batching
* AES-128-GCM authenticated encryption
* Stop-and-wait Automatic Repeat reQuest (ARQ) delivery
* Persistent sequence numbers using ESP32 NVS
* Real-time UI rendered on an SH1106 128×64 I2C OLED
* Hardware sidetone and TX/RX status indicators
* OTA firmware updates over Wi-Fi

---

## Features

### Morse Code Parsing Engine

Real-time Morse timing discrimination:

* `< 220 ms` → dot (`.`)
* `>= 220 ms` → dash (`-`)
* `600 ms` character completion timeout
* Binary lookup tree for Morse character decoding

### Batching & Outbox Queue

Characters are buffered into outgoing payloads.

A packet is transmitted automatically when:

* A space is inserted
* The payload reaches the 16-byte maximum
* The outbox has been idle for 1500 ms

### Authenticated Encryption

All data frames use:

* AES-128-GCM
* 128-bit encryption key
* 12-byte random nonces
* 16-byte authentication tags
* `esp_random()` for nonce generation
* ESP32 `mbedtls` cryptographic implementation

### ARQ Reliability Layer

Reliable delivery is implemented using stop-and-wait ARQ.

Features:

* 32-bit sequence numbers
* Sequence numbers persisted across reboots using ESP32 NVS
* 400 ms ACK timeout
* Up to 3 retransmissions per frame
* Duplicate frame suppression
* Explicit ACK frames

### Hardware Sidetone & Visuals

The device provides real-time feedback through:

* PWM/GPIO buzzer sidetone
* Dedicated TX LED
* Dedicated RX LED
* SH1106 128×64 OLED

The OLED displays telemetry including:

* RSSI
* Outbox buffer state
* Sequence counters
* Message history
* Transmission/reception state

### OTA Maintenance

Wi-Fi station connectivity supports background firmware updates using ArduinoOTA.

---

# Hardware Pinout

| Module / Component  | ESP32 GPIO | Description / Protocol        |
| ------------------- | ---------: | ----------------------------- |
| **LoRa SCK**        |    GPIO 18 | SPI Clock                     |
| **LoRa MISO**       |    GPIO 19 | SPI Master In Slave Out       |
| **LoRa MOSI**       |    GPIO 23 | SPI Master Out Slave In       |
| **LoRa CS**         |    GPIO 16 | Chip Select                   |
| **LoRa RST**        |    GPIO 26 | Hardware Reset                |
| **LoRa DIO0**       |    GPIO 25 | TX/RX Interrupt Input         |
| **OLED SDA**        |    GPIO 21 | I2C Data (SH1106)             |
| **OLED SCL**        |    GPIO 22 | I2C Clock (SH1106, 400 kHz)   |
| **Morse Key Input** |    GPIO 27 | Active LOW (`INPUT_PULLUP`)   |
| **Control Button**  |    GPIO 14 | Active LOW (`INPUT_PULLUP`)   |
| **Buzzer Output**   |    GPIO 33 | Active HIGH (Sidetone / Beep) |
| **TX LED**          |    GPIO 32 | Active HIGH                   |
| **RX LED**          |    GPIO 13 | Active HIGH                   |

---

# Protocol Specification

Each frame transmitted over the LoRa physical link is:

1. Framed
2. Encrypted
3. Authenticated
4. Transmitted using the configured LoRa parameters

The maximum physical frame size is **56 bytes**.

## Frame Layout

```text
+-------------------+--------------------+-------------------+
|    Header (24 B)  | Ciphertext (0-16B) |   Auth Tag (16 B) |
+-------------------+--------------------+-------------------+
```

### Frame Size

| Component          |         Size |
| ------------------ | -----------: |
| Header             |     24 bytes |
| Ciphertext         |   0–16 bytes |
| Authentication Tag |     16 bytes |
| **Maximum**        | **56 bytes** |

---

## Header Structure

| Offset      | Field               | Type          | Description                                   |
| ----------- | ------------------- | ------------- | --------------------------------------------- |
| `0x00`      | Magic Byte          | `uint8_t`     | Constant `0xC7` protocol identifier           |
| `0x01`      | Protocol Version    | `uint8_t`     | Constant `0x01`                               |
| `0x02`      | Packet Type         | `uint8_t`     | `1` = Data Frame, `2` = ACK                   |
| `0x03–0x04` | Source ID           | `uint16_t`    | Big-endian source node identifier             |
| `0x05–0x06` | Destination ID      | `uint16_t`    | Big-endian target node identifier             |
| `0x07–0x0A` | Sequence ID         | `uint32_t`    | Big-endian monotonic frame sequence number    |
| `0x0B–0x16` | Cryptographic Nonce | `uint8_t[12]` | Random nonce generated using `esp_random()`   |
| `0x17`      | Payload Length      | `uint8_t`     | Length `N` of plaintext payload, `0 ≤ N ≤ 16` |

---

# Packet Types

| Packet Type | Value | Purpose                                           |
| ----------- | ----: | ------------------------------------------------- |
| Data Frame  |   `1` | Encrypted application payload                     |
| ACK         |   `2` | Acknowledges successful reception of a data frame |

---

# Morse Engine

## Timing Rules

The Morse engine determines whether a key press represents a dot or dash based on its duration.

| Key Duration | Morse Symbol |
| -----------: | ------------ |
|   `< 220 ms` | Dot (`.`)    |
|  `>= 220 ms` | Dash (`-`)   |

For example:

```text
Short press  → .
Long press   → -
```

## Letter Separation

When no key press occurs for **600 ms**, the current Morse symbol sequence is considered complete.

The sequence is then decoded using the Morse binary lookup tree.

Example:

```text
.-

600 ms pause

→ A
```

---

# Outbox & Automatic Transmission

The transmitter buffers decoded characters in an outgoing queue.

A packet is automatically generated and transmitted when one of the following conditions occurs:

### 1. Space Insertion

A space indicates a word boundary and triggers transmission.

### 2. Payload Saturation

The payload reaches the maximum size:

```text
16 bytes
```

### 3. Idle Timeout

If the outbox contains data and remains idle for:

```text
1500 ms
```

the queued characters are automatically transmitted.

---

# Control Button

The control button is connected to GPIO 14 and uses:

```text
INPUT_PULLUP
```

The button is active LOW.

## Short Tap

A short tap:

1. Immediately flushes the current outbox.
2. Creates a data frame.
3. Encrypts the payload.
4. Transmits the frame over LoRa.

## Long Press

A long press is defined as:

```text
>= 500 ms
```

A long press:

1. Appends an explicit space character (`' '`) to the outbox.
2. Flushes the outbox if required.

---

# Encryption

LORA-CW uses authenticated encryption based on **AES-128-GCM**.

## Cryptographic Parameters

| Parameter                 | Value          |
| ------------------------- | -------------- |
| Algorithm                 | AES-128-GCM    |
| Key Size                  | 128 bits       |
| Nonce Size                | 12 bytes       |
| Authentication Tag        | 16 bytes       |
| Maximum Plaintext Payload | 16 bytes       |
| Nonce Source              | `esp_random()` |

The authentication tag ensures that modified or forged encrypted frames are rejected.

---

# Reliability & ARQ

LORA-CW implements a stop-and-wait ARQ protocol.

## Transmission Flow

```text
Sender                                  Receiver
  │                                        │
  │──── Encrypted DATA(seq=N) ───────────>│
  │                                        │
  │<──────────── ACK(seq=N) ──────────────│
  │                                        │
  │       Transmission complete            │
```

If the ACK is not received within the configured timeout:

```text
400 ms
```

the sender retransmits the frame.

## Retransmission Policy

Each frame may be retransmitted up to:

```text
3 times
```

If all attempts fail, the frame is considered undelivered.

## Duplicate Suppression

The receiver tracks received sequence numbers.

If a duplicate data frame is received:

1. The payload is not delivered twice.
2. The receiver responds appropriately with an ACK.
3. The duplicate is discarded.

---

# Sequence Numbers

Each data frame contains a 32-bit sequence number:

```text
uint32_t
```

Sequence numbers are monotonic and persisted using ESP32 NVS.

This allows sequence state to survive device reboots.

Example:

```text
Boot #1:
  TX seq = 100

Reboot

Boot #2:
  TX seq = 101
```

---

# OLED Interface

The device uses a:

```text
SH1106
128×64
I2C
400 kHz
```

OLED display.

The UI provides real-time telemetry including:

* Current transmission state
* Current reception state
* RSSI
* Outbox contents
* Sequence counters
* Message history
* ARQ status

---

# Wi-Fi & OTA

LORA-CW supports optional Wi-Fi connectivity for OTA firmware updates.

OTA functionality is controlled by:

```cpp
constexpr bool ENABLE_OTA = true;
```

When enabled, the ESP32 connects to the configured Wi-Fi network and exposes ArduinoOTA functionality.

---

# User Configuration

All operational parameters are defined in the firmware source under the **USER CONFIGURATION** section.

```cpp
// Device Addressing
constexpr uint16_t DEVICE_ID = 0x0002;
constexpr uint16_t PEER_DEVICE_ID = 0x0001;

// Cryptographic Key
// 32 hexadecimal characters = 128-bit AES key
constexpr char AES_KEY_HEX[] =
    "00112233445566778899AABBCCDDEEFF";

// LoRa RF Parameters
constexpr long LORA_FREQUENCY = 433000000L;
constexpr long LORA_BANDWIDTH = 125000L;
constexpr int LORA_SPREADING_FACTOR = 7;
constexpr int LORA_CODING_RATE = 5;
constexpr uint8_t LORA_SYNC_WORD = 0x12;

// Wi-Fi & OTA Updates
const char* WIFI_SSID = "YourSSID";
const char* WIFI_PASS = "YourPassword";
constexpr bool ENABLE_OTA = true;
```

## Device Addressing

Each device must have a unique node ID.

Example:

```text
Device A:
  DEVICE_ID      = 0x0001
  PEER_DEVICE_ID = 0x0002

Device B:
  DEVICE_ID      = 0x0002
  PEER_DEVICE_ID = 0x0001
```

## Cryptographic Key

Both devices must use the same AES-128 key.

The configured key contains:

```text
32 hexadecimal characters
```

which represents:

```text
128 bits
```

**Do not commit production cryptographic keys or Wi-Fi credentials to a public repository.**

---

# LoRa Configuration

Default configuration:

| Parameter        |     Value |
| ---------------- | --------: |
| Frequency        | `433 MHz` |
| Bandwidth        | `125 kHz` |
| Spreading Factor |     `SF7` |
| Coding Rate      |     `4/5` |
| Sync Word        |    `0x12` |

Configuration:

```cpp
constexpr long LORA_FREQUENCY = 433000000L;
constexpr long LORA_BANDWIDTH = 125000L;
constexpr int LORA_SPREADING_FACTOR = 7;
constexpr int LORA_CODING_RATE = 5;
constexpr uint8_t LORA_SYNC_WORD = 0x12;
```

The selected frequency and RF configuration must comply with the applicable local radio regulations.

---

# Required Dependencies

The project relies on the ESP32 Arduino framework and the following libraries.

| Dependency                 | Purpose                  |
| -------------------------- | ------------------------ |
| **U8g2** by Oliver Kraus   | OLED display driver      |
| **LoRa** by Sandeep Mistry | SX127x LoRa radio driver |
| **Preferences**            | ESP32 NVS storage        |
| **ArduinoOTA**             | OTA firmware updates     |
| **WiFi**                   | Wi-Fi connectivity       |
| **mbedTLS**                | AES-GCM cryptography     |

`Preferences`, `ArduinoOTA`, `WiFi`, and `mbedTLS` are provided by the ESP32 Arduino environment and do not normally require separate installation.

---

# Hardware Requirements

Each transceiver requires:

* ESP32 development board
* SX127x LoRa radio module
* SH1106 128×64 I2C OLED
* Morse key
* Control button
* Buzzer
* TX LED
* RX LED

The same firmware architecture can be used on both nodes by changing:

```cpp
DEVICE_ID
PEER_DEVICE_ID
```

and other node-specific configuration values.

---

# Software Setup

## Arduino IDE

1. Install the ESP32 board package.

2. Select:

   ```text
   ESP32 Dev Module
   ```

3. Install the required libraries.

4. Configure the firmware under the `USER CONFIGURATION` section.

5. Verify the GPIO assignments.

6. Connect the ESP32 through USB.

7. Compile the firmware.

8. Upload the firmware.

## PlatformIO

Configure the project for an ESP32 development board using the Arduino framework.

The project should provide the required dependencies through `platformio.ini`.

---

# Partition Scheme

The firmware requires NVS storage for persistent sequence numbers.

A suitable partition configuration is:

```text
Default 4MB with spiffs
```

or another partition profile that provides sufficient NVS storage.

---

# OTA Updates

Once the initial firmware has been uploaded over USB, subsequent firmware updates can be performed through ArduinoOTA when:

```cpp
constexpr bool ENABLE_OTA = true;
```

is enabled.

The ESP32 must be connected to the configured Wi-Fi network before OTA updates can be performed.

---

# Initial Deployment Checklist

Before flashing the firmware, verify:

* [ ] ESP32 board is correctly selected.
* [ ] LoRa module wiring matches the GPIO map.
* [ ] OLED wiring matches SDA/SCL configuration.
* [ ] Morse key is connected to GPIO 27.
* [ ] Control button is connected to GPIO 14.
* [ ] Buzzer is connected to GPIO 33.
* [ ] TX LED is connected to GPIO 32.
* [ ] RX LED is connected to GPIO 13.
* [ ] `DEVICE_ID` is unique.
* [ ] `PEER_DEVICE_ID` points to the intended remote device.
* [ ] Both devices use the same AES-128 key.
* [ ] LoRa frequency is appropriate for the deployment region.
* [ ] LoRa parameters match on both devices.
* [ ] Wi-Fi credentials are configured if OTA is enabled.
* [ ] NVS-capable partition scheme is selected.
* [ ] Production secrets are not committed to version control.

---

# Communication Example

Assume two devices:

```text
Node A
DEVICE_ID = 0x0001

Node B
DEVICE_ID = 0x0002
```

Node A enters:

```text
HELLO
```

The Morse engine decodes each character and places the resulting text into the outbox.

When the transmission conditions are met, the payload is encrypted and transmitted:

```text
Node A
  │
  │ DATA
  │ seq=42
  │ encrypted "HELLO"
  │
  ▼
Node B
  │
  │ decrypt
  │ authenticate
  │ validate sequence
  │ decode payload
  │
  ▼
  "HELLO"
  │
  │ ACK seq=42
  ▼
Node A
```

If the ACK is lost:

```text
Node A                         Node B
  │                              │
  │──── DATA seq=42 ───────────>│
  │                              │
  │<──────── ACK ────────X       │
  │                              │
  │     ACK timeout              │
  │                              │
  │──── DATA seq=42 ───────────>│
  │                              │
  │        duplicate             │
  │        suppressed            │
  │                              │
  │<──────── ACK seq=42 ─────────│
```

The receiver therefore does not deliver the same application payload twice.

---

# Protocol Constants

For quick reference:

| Parameter               |      Value |
| ----------------------- | ---------: |
| Protocol Magic          |     `0xC7` |
| Protocol Version        |     `0x01` |
| Data Packet Type        |        `1` |
| ACK Packet Type         |        `2` |
| Header Size             | `24 bytes` |
| Maximum Payload         | `16 bytes` |
| Authentication Tag      | `16 bytes` |
| Maximum Frame           | `56 bytes` |
| Nonce Size              | `12 bytes` |
| AES Key Size            | `128 bits` |
| Dot/Dash Threshold      |   `220 ms` |
| Character Timeout       |   `600 ms` |
| Outbox Idle Timeout     |  `1500 ms` |
| Long-Press Threshold    |   `500 ms` |
| ACK Timeout             |   `400 ms` |
| Maximum Retransmissions |        `3` |

---

# License

Add the project's license information here.

For example:

```text
MIT License
```

if the project is intended to be distributed under the MIT License.
