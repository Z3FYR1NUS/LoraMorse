# FIXES.md — what changed from the previous version, and why

This rewrite is **not compatible on-air** with the previous version: the
packet format, encryption scheme, and file layout all changed. Both ends of
a link must run this version.

## 1. Cryptography — replaced entirely

**Before:** `[8-byte nonce][1-byte ciphertext][1-byte tag]`. AES-128-ECB of
the nonce (zero-padded) was used as a one-shot keystream; the plaintext byte
was XORed with keystream byte 0; keystream byte 1 was reused as a "tag."

Problems this had:
- The "tag" was one byte wide -> only ~8 bits of forgery resistance. An
  attacker could forge a valid-looking packet by brute-forcing roughly 256
  attempts.
- No sequence number anywhere -> a captured packet could be replayed
  verbatim and would decrypt/authenticate as valid again.
- Not a standard construction, so no existing analysis or tooling applies to
  it.

**After:** AES-128-CTR encrypts the payload; HMAC-SHA256 (truncated to 10
bytes / 80 bits) authenticates the packet header plus ciphertext
(Encrypt-then-MAC). Nonce is 96 bits, fresh and random per packet via the
ESP32 hardware TRNG (`esp_random()`). See `src/crypto/Crypto.{h,cpp}` and
the README's "Encryption / authentication" section.

Verified with unit tests: roundtrip correctness, ciphertext-differs-from-
plaintext, tampered-ciphertext rejection, tampered-header rejection,
wrong-key rejection, nonce-uniqueness, and zero-length payload handling —
`test/test_crypto`.

## 2. Radio reliability — added from scratch

**Before:** no ACK, no retransmission, no addressing, no sequence numbers.
"TX happened" was the only signal available, and it was conflated with
"the message arrived."

**After:**
- `src/protocol/Packet.{h,cpp}` adds a real header: protocol version, packet
  type (DATA/ACK), source/destination device ID, 32-bit sequence number.
- `App`'s reliability state machine (`src/app/App.cpp`) implements
  KEY→QUEUE→TX→WAIT_ACK→DELIVERED, with timeout-triggered RETRY (up to
  `link_cfg::MAX_RETRIES`, default 4, linear backoff) and FAILED once
  retries are exhausted.
- The footer now reports `DELIVERED <letter>`, `RETRY n`, and `DELIVERY
  FAILED` as distinct states. **The TX LED still means only "RF
  transmission occurred," exactly as before** — it is documented as such
  everywhere it's referenced, specifically so it isn't mistaken for a
  delivery indicator.
- `src/protocol/ReplayGuard.{h,cpp}` rejects any DATA packet whose sequence
  number isn't higher than the last one accepted from that sender, closing
  the replay gap. Verified in `test/test_replay`.
- `src/system/SeqStore.{h,cpp}` persists the outgoing sequence counter in
  NVS in batches of 32, so sequence numbers are never reused across a
  reboot (which would otherwise undermine the replay check) — see the
  README's "Replay protection" section for the batching trade-off.

## 3. Architecture — split into modules

**Before:** one ~870-line `src/main.cpp` containing radio, crypto, morse,
display, WiFi/OTA, and input handling together, plus:

```cpp
#define private public
#include <LoRa.h>
#undef private
```

**Removed.** Every LoRa call this project actually uses (`begin`, `setPins`,
`beginPacket`, `endPacket`, `write`, `parsePacket`, `available`, `read`,
`isTransmitting`, `idle`, `packetRssi`) is part of the library's public API
(`packetSnr` used for the newly added SNR readout is public too) — the hack
was not needed for anything present in the sketch. Reaching into a library's
private internals is fragile across library versions regardless of whether
it was needed at the time it was written.

**After:** `src/app`, `src/crypto`, `src/protocol`, `src/radio`,
`src/morse`, `src/input`, `src/display`, `src/system`, with `src/main.cpp`
reduced to a two-line `setup()`/`loop()` that delegates to `App`. The
existing dirty-tile OLED update design was preserved as-is — it was already
good — just decoupled from direct access to `App`'s internals via a plain
`UiState` snapshot struct (`src/display/Display.h`).

## 4. Secrets — moved out of source

**Before:** WiFi credentials and the AES key lived directly in `main.cpp`.

**After:** `include/config.h` holds only non-secret build configuration
(pins, timing, radio parameters) and is safe to commit. `include/secrets.h`
holds WiFi credentials, the OTA password, the PSK, and device IDs; it's
listed in `.gitignore` and ships only as a template,
`include/secrets.example.h`, that you copy and fill in yourself.

## 5. Testing — added

**Before:** none.

**After:** `test/test_morse`, `test/test_packet`, `test/test_crypto`,
`test/test_replay` — PlatformIO/Unity tests covering everything in
`src/crypto`, `src/protocol`, and `src/morse` that has no Arduino/hardware
dependency. Run with:

```sh
pio test -e native
```

**Honesty note on verification:** in the environment this rewrite was
produced in, `pio test -e native` itself couldn't complete — PlatformIO
needed to download its "native" platform package from
`api.registry.platformio.org`, which that environment's network egress
doesn't allow. To actually verify the logic rather than just write tests and
hope, `libmbedtls-dev` and the Unity test framework source were installed
directly (`apt`, and cloning `github.com/ThrowTheSwitch/Unity`), and each
test file was compiled and run by hand with `g++` against the real
`Crypto.cpp` / `Packet.cpp` / `ReplayGuard.cpp` / `Morse.cpp` source files
and real mbedTLS. **All 26 test cases passed.** This is equivalent in
substance to `pio test -e native` (same source files, same test files, same
mbedTLS API family — desktop mbedTLS 2.28 vs. the ESP32 core's bundled
mbedTLS), just invoked manually instead of through PlatformIO's test
runner. You should still run `pio test -e native` yourself once you have
normal network access, since that's the officially supported path and this
manual invocation isn't something the project depends on going forward.

`Button`, `Radio`, `Display`, `WifiManager`, `OtaManager`, `SeqStore`, and
`App` are **not covered by any test, native or otherwise**, and **the
ESP32-hardware-facing build has not been compiled** in this rewrite's
environment — that would require downloading the `espressif32` platform and
the `LoRa`/`U8g2` libraries from PlatformIO's registry, which also wasn't
reachable. That code was written carefully and reviewed by hand (and one
real bug — see below — was caught and fixed during that review), but "hand
reviewed" is not "compiled and run." **Build with `pio run -e esp32dev` and
test on your actual hardware before relying on this.**

## 6. Radio configuration — made explicit

**Before:** relied on `sandeepmistry/LoRa`'s library defaults for
bandwidth, spreading factor, coding rate, sync word, and TX power.

**After:** `include/config.h`'s `radio_cfg` namespace sets all of these
explicitly (`radio_cfg::SPREADING_FACTOR`, `BANDWIDTH_HZ`,
`CODING_RATE_DENOM`, `SYNC_WORD`, `TX_POWER_DBM`, `PREAMBLE_LENGTH`,
`ENABLE_CRC`), applied in `Radio::begin()`. Both ends of a link must use
matching values — see the README's "Configuration reference" table.

## 7. Diagnostics — expanded

**Before:** RSSI only.

**After:** SNR added (`Radio::lastSnr()`, `LoRa.packetSnr()` — public API).
`App::Diagnostics` (`src/app/App.h`) counts TX attempts, deliveries,
retries, failures, bad/unauthenticated packets, and replayed-packet
rejections, for anyone wiring up additional status output later (not yet
surfaced anywhere beyond the footer's transient status text — see
"Known simplifications" below).

## 8. OTA — hardened

**Before:** an empty OTA password was accepted by the library, which means
any device on the network could push firmware.

**After:** `App::loadSecrets()` halts at boot with a clear serial message if
`SECRET_OTA_PASSWORD` is empty. There's no way to run this firmware with OTA
unprotected short of deliberately weakening `secrets.h` yourself.

## Known simplifications / things intentionally not built

Listed here so they're a documented decision, not a silent gap:

- **Stop-and-wait only** (one outstanding DATA packet at a time), which is
  why `ReplayGuard` only needs a single monotonic counter per peer rather
  than a sliding window/bitmap. If this project ever needs multiple
  outstanding packets, that's the piece that would need to change.
- **Two-device addressing.** `Packet`'s header supports arbitrary source/
  destination IDs and `ReplayGuard` tracks up to `MAX_PEERS` (default 4)
  senders independently, but `App`'s send/receive logic is written for
  exactly one configured peer. A real multi-device mesh would need routing
  logic this project doesn't attempt.
- **PING/STATUS packet types** mentioned as a possible future addition were
  not added — only DATA and ACK exist. `PacketType` and the wire format's
  version byte leave room to add more later without breaking the framing.
- **Diagnostics counters exist but aren't surfaced in the UI beyond
  transient status text** — `App::Diagnostics` is populated but there's no
  dedicated stats screen. Straightforward to add if useful; left out to
  keep this rewrite's scope to what was asked for.

## Bug found and fixed during this rewrite's own review

While reviewing the reliability state machine, `App::beginTxFromQueue`
originally popped the queued token off the TX queue *before* confirming
`Radio::beginPacket()` succeeded. If the radio was momentarily busy (e.g.
mid-receive) at exactly that instant, the queued token would be silently
dropped instead of retried. Fixed by peeking the queue and only dequeuing
once a real `beginPacket()` + `sendPacket()` has actually gone out. Flagged
here rather than silently folded into "the code" so it's clear this rewrite
went through at least one real self-review pass, not just a first draft.
