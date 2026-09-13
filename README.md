# LORA-CW

A standalone Morse code (CW) key/receiver for ESP32, using a LoRa radio for
the RF link and an SH1106 128x64 OLED for the UI. RF packets are encrypted
and authenticated (AES-128-CTR + HMAC-SHA256), sequenced, and acknowledged
with automatic retry. Supports over-the-air (OTA) firmware updates over WiFi
once the initial upload is done via USB.

See [FIXES.md](FIXES.md) for exactly what changed from the earlier version
of this project and why.

## Features

- Morse key input with automatic dot/dash timing and letter/word segmentation
- **Authenticated-encrypted, sequenced, acknowledged LoRa link** — every
  packet is AES-128-CTR + HMAC-SHA256 protected, replay-checked per sender,
  and retried automatically if it isn't acknowledged
- SH1106 OLED UI: live TX/RX cards, timing progress bar, scrolling
  received-message log, RSSI bars, WiFi status, status/footer line
- Local sidetone buzzer feedback on key-down and on received marks
- TX/RX LED indicators (**local transmit/receive activity only** — see
  [Delivery semantics](#delivery-semantics-ack--retry))
- Background WiFi connection with automatic retry, and OTA firmware updates,
  now requiring a real OTA password
- Partial-display updates (dirty-tile diffing) to keep the I2C bus and UI
  responsive without blocking key/radio timing
- Modular source layout (`src/app`, `src/crypto`, `src/protocol`,
  `src/radio`, `src/morse`, `src/input`, `src/display`, `src/system`) instead
  of one file
- Native PlatformIO unit tests for the protocol-level logic (crypto framing,
  packet encode/decode, replay guard, Morse decode)

## Hardware

| Component | Notes |
|---|---|
| ESP32 dev board | Any standard ESP32 devkit |
| SX1278 LoRa module | 433 MHz variant expected |
| SH1106 128x64 OLED | I2C, hardware I2C driver |
| Morse key (paddle/switch) | Wired to `KEY_INPUT`, active-low |
| Control button | Wired to `CONTROL_INPUT`, active-low |
| Buzzer | Active buzzer expected; see [Hardware notes](#hardware-notes) for passive piezos |
| TX / RX LEDs | Simple indicator LEDs |

### Pinout (default, `include/config.h`)

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

All pins live in `namespace pins` in `include/config.h` and can be changed
there if your wiring differs — nothing else in the source needs to change.

## Dependencies

Declared in `platformio.ini` under `lib_deps`:

- [LoRa by Sandeep Mistry](https://github.com/sandeepmistry/arduino-LoRa) — `^0.8.0`
- [U8g2 by olikraus](https://github.com/olikraus/u8g2) — `^2.35.19`
- ESP32 Arduino core's built-in `WiFi`, `ArduinoOTA`, and `Preferences` libraries
- ESP32 Arduino core's bundled **mbedTLS** (`mbedtls/aes.h`, `mbedtls/md.h`) for
  AES-128-CTR and HMAC-SHA256, and `esp_random()` (hardware TRNG) — no extra
  dependency needed on-device

## Project layout

```
LORA-CW/
├── include/
│   ├── config.h            non-secret pins/timing/radio/protocol tuning
│   └── secrets.example.h   template -> copy to secrets.h (gitignored)
├── src/
│   ├── main.cpp             thin Arduino entry point
│   ├── app/                 App: orchestration, setup()/loop(), reliability state machine
│   ├── crypto/               AES-128-CTR + HMAC-SHA256 packet encryption/auth
│   ├── protocol/             Packet framing + ReplayGuard
│   ├── radio/                LoRa wrapper with explicit radio configuration
│   ├── morse/                Mark buffer + dot/dash decode tree
│   ├── input/                Debounced Button
│   ├── display/              SH1106 UI with dirty-tile partial updates
│   └── system/                WifiManager, OtaManager, SeqStore (persisted TX sequence)
└── test/                     Native PlatformIO Unity tests (protocol-level logic only)
```

## Building and flashing (PlatformIO)

This project ships three environments:

- **`env:esp32dev`** — standard USB/serial upload, used for the first flash
- **`env:ota`** — wireless upload over WiFi via `espota`, extends `env:esp32dev`
- **`env:native`** — desktop unit tests, no ESP32 hardware involved (see [Testing](#testing))

### First flash (USB)

**1. Copy the secrets template and fill it in:**

```sh
cp include/secrets.example.h include/secrets.h
```

Edit `include/secrets.h`:

```cpp
#define SECRET_WIFI_SSID   "your-ssid"
#define SECRET_WIFI_PASS   "your-password"
#define SECRET_OTA_PASSWORD "a-real-password"   // required, not optional
#define SECRET_PSK_HEX "9f2c8a3e1d4b6f705c9a2e8d1b4f6073"  // replace this
#define SECRET_THIS_DEVICE_ID 1
#define SECRET_PEER_DEVICE_ID 2
```

`include/secrets.h` is gitignored — it will never be committed as long as you
don't rename it.

**2. Generate a real pre-shared key on your own machine** (never paste real
keys into chat, tickets, commit messages, or anywhere else they could leak):

```sh
openssl rand -hex 16
```

Paste the resulting 32-character hex string into `SECRET_PSK_HEX`. The
firmware halts at boot with a serial error if this string is the wrong
length or contains a non-hex character, rather than silently running with a
broken key.

**3. Pick device IDs.** Every device on the link needs a distinct, non-zero
ID. For a simple two-radio link, device "A" uses `THIS_DEVICE_ID=1,
PEER_DEVICE_ID=2` and device "B" uses the mirror image (`2` / `1`).

**4. Build and upload over USB:**

```sh
pio run -e esp32dev -t upload
pio device monitor -b 115200
```

> The board must use an **OTA-capable partition scheme** — `platformio.ini`
> already sets `board_build.partitions = default_ota.csv` for this.

### Subsequent updates (OTA / WiFi)

Once the device has joined your WiFi network (check the serial monitor or
the OLED footer for its IP address), either export the OTA password as an
environment variable before uploading:

```sh
export LORA_CW_OTA_PASSWORD="the same password you put in secrets.h"
```

or edit `platformio.ini`'s `[env:ota]` section directly. Then set the
device's current IP as the upload port and upload:

```ini
[env:ota]
extends = env:esp32dev
upload_protocol = espota
upload_port = 192.168.1.42   ; replace with your device's IP
```

```sh
pio run -e ota -t upload
```

While an OTA update is in progress, the device suspends normal key/radio
operation, silences all outputs, and shows a progress screen on the OLED.
**Keep the device powered during the update.**

## Controls

| Action | Result |
|---|---|
| Tap **KEY**, release under 250 ms | Sends a dot |
| Tap **KEY**, hold 250 ms or more | Sends a dash |
| Pause 850 ms after the last mark | Ends the current letter (auto-sent) |
| Tap **CONTROL** (release before 700 ms) | Clears local input/display buffers |
| Hold **CONTROL** for 700 ms | Sends a word space |

Clearing the buffers only affects local/pending state — any letter already
transmitted over LoRa cannot be recalled, and the peer will still process
whatever it already received or has queued.

## Display layout

- **Header** — title, LoRa status ("433" or "RF!"), RSSI bars + value (or
  "RX --" if nothing received yet), WiFi status bars/X
- **TX card** — current outgoing dot/dash pattern, decoded letter preview,
  lit while keying or transmitting
- **RX card** — current incoming dot/dash pattern, last decoded letter, lit
  briefly on reception
- **Progress bar** — fills to show elapsed time toward the current threshold
  (dot/dash split, control hold, or letter-pause timeout)
- **Log** — last two lines (up to 48 characters) of the received message
- **Footer** — rotates between radio/WiFi status, transient status messages
  (`DELIVERED <letter>`, `RETRY n`, `DELIVERY FAILED`, `BAD/UNAUTH PACKET`,
  `REPLAY/UNKNOWN PEER`, etc.), TX queue size, and the device's IP address

## Protocol format

Every on-air frame (all multi-byte integers big-endian):

```
byte 0       version      protocol version (currently 1)
byte 1       type         1 = DATA, 2 = ACK
byte 2       src          sending device ID (1-255)
byte 3       dst          destination device ID
bytes 4-7    seq          32-bit sequence number, monotonically increasing per sender
bytes 8-19   nonce        12-byte random nonce
byte 20      payloadLen   0 (ACK) or 1 (DATA: one Morse token)
bytes 21..   ciphertext   payloadLen bytes, AES-128-CTR
last 10      tag          HMAC-SHA256, truncated to 10 bytes (80 bits)
```

A DATA packet's payload is a single Morse token: `.`, `-`, `/` (end of
letter), or ` ` (word space). An ACK carries no payload — it exists purely
to name the sequence number it's acknowledging.

The header (bytes 0-20) is sent in the clear — the receiver needs it to even
know how to parse and route the packet — but it is authenticated as
associated data together with the ciphertext. Altering any header byte in
transit, or without the shared key, breaks the tag and the whole packet is
dropped as `BAD/UNAUTH PACKET`.

## Encryption / authentication

This replaces a previous **custom, non-standard scheme**
(`AES-128-ECB(nonce)` used as a one-shot keystream generator, XORed with a
single plaintext byte, with a second keystream byte reused as an 8-bit
"tag") that gave only about 8 bits of forgery resistance and no protection
against a captured packet being replayed later.

The current construction is a standard Encrypt-then-MAC:

- **Ciphertext** = `AES-128-CTR(key, nonce || 0x00000000, payload)`. The
  96-bit nonce is fresh, random (`esp_random()`, the ESP32 hardware TRNG) on
  every packet; the block counter starts at 0 and never needs to advance
  more than once since a payload is at most a few bytes.
- **Tag** = the first 10 bytes (80 bits) of `HMAC-SHA256(key, header ||
  ciphertext)`. The receiver recomputes this independently; a mismatch drops
  the packet before it is ever decrypted or handed to the decoder.
- Comparison of the received tag against the recomputed one is constant-time
  (every byte is checked regardless of where a mismatch occurs), so a timing
  side channel can't be used to guess a valid tag one byte at a time.

This is a conventional, well-understood building block (not a bespoke
cipher mode), backed by mbedTLS's audited AES and SHA-256 implementations
that are already part of the ESP32 Arduino core.

### Key setup

1. Generate 32 hex characters with `openssl rand -hex 16`.
2. Put that string in `SECRET_PSK_HEX` in your own `include/secrets.h` (never
   in `config.h`, which is meant to be committed).
3. At boot, the firmware decodes it into the working AES key and halts with
   a serial error if the string is the wrong length or contains invalid
   characters.
4. Every device on the link must have the **exact same** `SECRET_PSK_HEX`
   and a **distinct** `SECRET_THIS_DEVICE_ID`.

Implications:

- **Not compatible with an unmodified/original peer**, and not compatible
  with the previous version of this firmware — the wire format changed.
- **Packet size increased** — a DATA packet is now 32 bytes on air (versus
  10 bytes previously), plus a 31-byte ACK for every DATA packet. This is
  still trivial relative to human keying speed, but it does roughly triple
  per-character airtime; see [Limitations](#limitations--security-notes) if
  you're on a very congested band.
- **Never paste a real key into chat tools, tickets, commit messages, or
  anywhere else outside the device's own `secrets.h`** — treat any key that
  has been typed somewhere else as compromised and regenerate it.

## Replay protection

Each sender's sequence number must strictly increase from the receiver's
point of view. `ReplayGuard` tracks the highest sequence number seen per
peer device ID:

- A new, higher sequence number is accepted and processed.
- An exact repeat of the last accepted sequence number is treated as a
  duplicate delivery (e.g. the original ACK was lost) — it is re-acknowledged
  but **not** reprocessed a second time.
- Anything lower than the last accepted sequence number is rejected outright
  as a replay.

Because this link only ever has one outstanding DATA packet at a time
(stop-and-wait, see below), a single "highest sequence seen" counter per
peer is enough — there's no need for a sliding replay window/bitmap the way
a link with multiple outstanding packets would need.

Sequence numbers are persisted in NVS in batches of 32 (see `SeqStore`), so
a reboot can never reuse a number the peer has already seen. On an unclean
reboot, up to 31 sequence numbers are permanently skipped — harmless, since
the counter is 32 bits wide.

## Delivery semantics (ACK / retry)

```
KEY PRESS -> QUEUE -> TX -> WAIT ACK -> ACK received  -> DELIVERED
                               |
                               +-- timeout, retries remain -> RETRY (resend same packet)
                               +-- timeout, retries exhausted -> FAILED
```

- **TX** means the ESP32 handed a frame to the LoRa radio and the radio
  reports it went out over the air. This is what the **TX LED reflects —
  and nothing more.**
- **DELIVERED** means an authenticated ACK for that exact sequence number
  came back from the peer. Only this — never TX/LED activity — means the
  peer actually received and accepted the packet.
- A retry resends the **identical** frame (same sequence number and nonce)
  — it's a resend of one logical message, not a new packet, which is also
  why the receiver correctly treats a second copy as a duplicate rather than
  a replay attack.
- After `link_cfg::MAX_RETRIES` (default 4) resends with no ACK, the item is
  marked FAILED and the queue moves on. The footer reports `DELIVERY
  FAILED`.
- This link is **stop-and-wait**: only one DATA packet is ever in flight at
  a time. The next queued token isn't sent until the current one is
  DELIVERED or FAILED.

## Configuration reference

`include/config.h` (safe to commit — no secrets):

| Constant | Purpose |
|---|---|
| `radio_cfg::FREQUENCY_HZ` / `TX_POWER_DBM` / `SPREADING_FACTOR` / `BANDWIDTH_HZ` / `CODING_RATE_DENOM` / `SYNC_WORD` / `PREAMBLE_LENGTH` / `ENABLE_CRC` | LoRa radio parameters; both ends of a link must agree on all of them |
| `link_cfg::ACK_TIMEOUT_MS` | Base wait for an ACK before retrying; increase if you raise `SPREADING_FACTOR` or lower `BANDWIDTH_HZ` |
| `link_cfg::MAX_RETRIES` / `RETRY_BACKOFF_MS` | Retry count and linear backoff between attempts |
| `link_cfg::TX_START_TIMEOUT_MS` | Guards against a stuck radio transaction, independent of the ACK wait |
| `timing::*` | Morse timing, UI refresh rate, WiFi connect/retry timing — unchanged from the original sketch |
| `display_cfg::I2C_HZ` | OLED I2C bus speed; lower to `100000` if your module/wiring needs it |
| `app_cfg::OTA_HOSTNAME` | mDNS hostname used for OTA discovery |

`include/secrets.h` (gitignored — never commit):

| Constant | Purpose |
|---|---|
| `SECRET_WIFI_SSID` / `SECRET_WIFI_PASS` | WiFi credentials |
| `SECRET_OTA_PASSWORD` | Required, non-empty OTA password |
| `SECRET_PSK_HEX` | 32-char hex AES-128 key, identical on every device on the link |
| `SECRET_THIS_DEVICE_ID` / `SECRET_PEER_DEVICE_ID` | Device addressing, distinct per device |

## Testing

Native, hardware-free unit tests cover the protocol-level logic that doesn't
depend on Arduino/LoRa/U8g2/WiFi:

```sh
pio test -e native
```

This exercises:

- `test/test_morse` — dot/dash -> letter decode tree, mark buffer overflow behavior
- `test/test_crypto` — encrypt/decrypt roundtrip, tamper detection (ciphertext and
  header), wrong-key rejection, nonce uniqueness
- `test/test_packet` — frame encode/decode roundtrip for DATA and ACK, forged/
  truncated/bad-version frame rejection
- `test/test_replay` — new/duplicate/replay/unknown-peer verdicts, independent
  per-peer tracking

`Button`, `Radio`, `Display`, `WifiManager`, `OtaManager`, `SeqStore`, and
`App` are **not** covered by native tests — they depend on Arduino.h, the
LoRa/U8g2 libraries, WiFi, or NVS, none of which exist on a desktop. Those
are only exercised by actually running the firmware on hardware. See
[Limitations](#limitations--security-notes) for exactly what has and hasn't
been verified for this rewrite.

## Hardware notes

- The buzzer is assumed to be **active** (driven simply HIGH/LOW). If you
  use a **passive piezo**, drive `pins::BUZZER` with PWM/tone output instead
  of a digital HIGH/LOW.
- Radio TX and WiFi connection waits are cooperative (non-blocking in the
  main loop), but LoRa SPI transfers, radio initialization, AES/HMAC
  operations, and the actual OTA flash write are synchronous and will
  briefly block.

## Troubleshooting

- **Device halts at boot printing `[FATAL] SECRET_PSK_HEX must be...`** —
  `SECRET_PSK_HEX` in `secrets.h` is missing, the wrong length, or has a
  typo. Generate a fresh key with `openssl rand -hex 16`.
- **Device halts at boot printing `[FATAL] SECRET_OTA_PASSWORD must not be
  empty`** — set a real OTA password in `secrets.h`; an empty one is no
  longer accepted.
- **"RADIO OFFLINE" / "RF!" in header** — check LoRa module wiring (SPI
  pins, CS/RST/DIO0) and frequency match with the peer.
- **"BAD/UNAUTH PACKET" in footer** — the peer's key doesn't match yours, or
  the packet was corrupted/foreign. Confirm both devices run this firmware
  version and share the identical `SECRET_PSK_HEX`.
- **"REPLAY/UNKNOWN PEER" in footer** — either a genuinely replayed old
  packet, or more likely during bring-up: `SECRET_THIS_DEVICE_ID` /
  `SECRET_PEER_DEVICE_ID` are misconfigured on one side, or the replay
  table (`ReplayGuard::MAX_PEERS`, default 4) is full of stale unknown
  peers on a noisy shared frequency.
- **Everything eventually shows "DELIVERY FAILED"** — the peer isn't
  receiving or isn't answering with a valid ACK. Check that both devices
  share the same radio parameters in `config.h` (SF/BW/coding
  rate/sync word), not just the key.
- **OLED shows nothing** — verify I2C wiring and try lowering
  `display_cfg::I2C_HZ` to `100000`.
- **WiFi never connects** — Morse/LoRa functionality still works without
  WiFi; OTA simply won't be available until the device joins the network.
- **OTA upload fails** — confirm the board's partition scheme supports OTA,
  the device is on the same network, `upload_port` matches its current IP,
  and `LORA_CW_OTA_PASSWORD` (or the hardcoded `--auth` value) matches
  `SECRET_OTA_PASSWORD`.

## Limitations / security notes

Read this section before relying on this project for anything sensitive.

- **This link provides confidentiality, per-packet authenticity, and replay
  protection for a two-party stop-and-wait link. It has not undergone any
  external/professional security review.** Standard building blocks
  (AES-CTR, HMAC-SHA256) are used correctly to the best of this rewrite's
  understanding, but "correctly used standard primitives" is not the same
  as "audited protocol."
- **The pre-shared key model has no key rotation.** Every device on the link
  shares one static AES key indefinitely. If that key is ever exposed,
  every past and future packet encrypted with it is compromised (no
  forward secrecy).
- **Only two device IDs are meaningfully supported end-to-end** in the
  current App logic (`THIS_DEVICE_ID` and one `PEER_DEVICE_ID`), even though
  `ReplayGuard` can track up to `MAX_PEERS` senders and the wire format has
  room for arbitrary addressing. Extending to a real multi-device network
  would need addressing/routing logic this project doesn't have.
- **Stop-and-wait only.** One outstanding DATA packet at a time. This keeps
  the replay check simple (a single monotonic counter) but caps throughput
  to one round-trip per Morse token — fine for a human keying speed, not
  suitable as a general-purpose reliable link for bulk data.
- **Denial of service is not addressed.** An attacker who can transmit on
  the same frequency/sync word can still jam the link or flood it with
  garbage that gets dropped as `BAD/UNAUTH PACKET` — dropping forged
  packets safely is not the same as preventing an attacker from occupying
  the channel.
- **This rewrite's ESP32-hardware-facing code (`App`, `Radio`, `Display`,
  `Button`, `WifiManager`, `OtaManager`, `SeqStore`) has been carefully
  reviewed but could not be build-verified against the real
  espressif32/LoRa/U8g2 toolchain in the environment this rewrite was
  produced in** (that environment's network access doesn't reach the
  PlatformIO package/board registries). Only the protocol-level logic
  (`crypto/`, `protocol/`, `morse/`) has actually been compiled and
  unit-tested, against real mbedTLS, with all tests passing. **Build this
  with `pio run -e esp32dev` and test on real hardware before trusting it**,
  the same as you should for any firmware from any source.
- **TX LED != delivery**, worth repeating: it lights on RF transmission
  only. Use the footer's `DELIVERED` / `RETRY n` / `DELIVERY FAILED`
  messages for actual delivery status.
