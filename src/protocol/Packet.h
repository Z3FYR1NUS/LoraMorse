#pragma once
// Packet -- on-air framing for LORA-CW.
//
// Replaces the previous bare [nonce][ciphertext-byte][tag-byte] frame with a
// proper addressed, versioned, sequenced, authenticated header. This is what
// makes ACKs, retries, and replay protection possible: the previous format
// had no sequence number, no packet type, and no addressing at all.
//
// Wire layout (all multi-byte integers big-endian):
//
//   byte 0       version         protocol version (PROTOCOL_VERSION)
//   byte 1       type            PacketType
//   byte 2       src             sending device ID (1-255; 0 reserved/invalid)
//   byte 3       dst             destination device ID
//   bytes 4-7    seq             sequence number, monotonically increasing per sender
//   bytes 8-19   nonce           12-byte random nonce (Crypto::NONCE_LEN)
//   byte 20      payloadLen      0..MAX_PAYLOAD_LEN
//   bytes 21..   ciphertext      payloadLen bytes (AES-128-CTR)
//   last 10      tag             Crypto::TAG_LEN, HMAC-SHA256 truncated
//
// The header (bytes 0-20) is sent in the clear but is authenticated as
// associated data along with the ciphertext -- a receiver rejects the whole
// packet (via Crypto::decryptAndVerify) if anything in the header or
// ciphertext was altered in transit or forged without the shared key.

#include <stddef.h>
#include <stdint.h>

#include "crypto/Crypto.h"

enum class PacketType : uint8_t {
    DATA = 1,  // carries one Morse token ('.', '-', '/', or ' ') as its payload
    ACK  = 2,  // acknowledges a DATA packet's sequence number; empty payload
};

constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr uint8_t MAX_PAYLOAD_LEN  = 4;

struct PacketHeader {
    uint8_t version = PROTOCOL_VERSION;
    PacketType type = PacketType::DATA;
    uint8_t src = 0;
    uint8_t dst = 0;
    uint32_t seq = 0;
    uint8_t nonce[Crypto::NONCE_LEN] = {0};
    uint8_t payloadLen = 0;
};

class Packet {
public:
    static constexpr size_t HEADER_LEN =
        1 + 1 + 1 + 1 + 4 + Crypto::NONCE_LEN + 1;  // = 21
    static constexpr size_t MIN_WIRE_LEN = HEADER_LEN + Crypto::TAG_LEN;
    static constexpr size_t MAX_WIRE_LEN = MIN_WIRE_LEN + MAX_PAYLOAD_LEN;

    // Encodes and authenticates/encrypts a packet into `out` (capacity
    // `outCap`). `nonce` must be fresh randomness (Crypto::NONCE_LEN bytes)
    // supplied by the caller -- Packet itself has no RNG dependency, which
    // keeps it testable without hardware. Returns the number of bytes
    // written, or 0 on error (bad payloadLen or insufficient outCap).
    static size_t encode(const Crypto& crypto, PacketType type,
                          uint8_t src, uint8_t dst, uint32_t seq,
                          const uint8_t nonce[Crypto::NONCE_LEN],
                          const uint8_t* payload, uint8_t payloadLen,
                          uint8_t* out, size_t outCap);

    // Parses and authenticates a received frame. Returns true only if the
    // length is well-formed AND the authentication tag verifies; on false,
    // `header`/`payloadOut` are left untouched. `payloadOut` must have room
    // for MAX_PAYLOAD_LEN bytes.
    static bool decode(const Crypto& crypto, const uint8_t* in, size_t inLen,
                        PacketHeader* header, uint8_t* payloadOut);
};
