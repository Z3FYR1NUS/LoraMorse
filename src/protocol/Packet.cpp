#include "Packet.h"

#include <string.h>

namespace {
void putU32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
}
uint32_t getU32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
}  // namespace

size_t Packet::encode(const Crypto& crypto, PacketType type,
                       uint8_t src, uint8_t dst, uint32_t seq,
                       const uint8_t nonce[Crypto::NONCE_LEN],
                       const uint8_t* payload, uint8_t payloadLen,
                       uint8_t* out, size_t outCap) {
    if (payloadLen > MAX_PAYLOAD_LEN) return 0;
    const size_t total = HEADER_LEN + payloadLen + Crypto::TAG_LEN;
    if (outCap < total) return 0;

    out[0] = PROTOCOL_VERSION;
    out[1] = uint8_t(type);
    out[2] = src;
    out[3] = dst;
    putU32(out + 4, seq);
    memcpy(out + 8, nonce, Crypto::NONCE_LEN);
    out[20] = payloadLen;

    uint8_t* ciphertext = out + HEADER_LEN;
    uint8_t* tag = ciphertext + payloadLen;
    crypto.encrypt(nonce, out, HEADER_LEN, payload, payloadLen, ciphertext, tag);
    return total;
}

bool Packet::decode(const Crypto& crypto, const uint8_t* in, size_t inLen,
                     PacketHeader* header, uint8_t* payloadOut) {
    if (inLen < MIN_WIRE_LEN || inLen > MAX_WIRE_LEN) return false;

    const uint8_t payloadLen = in[20];
    const size_t expected = HEADER_LEN + payloadLen + Crypto::TAG_LEN;
    if (inLen != expected) return false;
    if (in[0] != PROTOCOL_VERSION) return false;
    const uint8_t rawType = in[1];
    if (rawType != uint8_t(PacketType::DATA) && rawType != uint8_t(PacketType::ACK))
        return false;

    const uint8_t* nonce = in + 8;
    const uint8_t* ciphertext = in + HEADER_LEN;
    const uint8_t* tag = ciphertext + payloadLen;

    uint8_t plaintext[MAX_PAYLOAD_LEN] = {0};
    if (!crypto.decryptAndVerify(nonce, in, HEADER_LEN, ciphertext, payloadLen, tag,
                                  plaintext)) {
        return false;
    }

    header->version = in[0];
    header->type = PacketType(rawType);
    header->src = in[2];
    header->dst = in[3];
    header->seq = getU32(in + 4);
    memcpy(header->nonce, nonce, Crypto::NONCE_LEN);
    header->payloadLen = payloadLen;
    if (payloadLen) memcpy(payloadOut, plaintext, payloadLen);
    return true;
}
