#include <unity.h>

#include "protocol/Packet.h"

void setUp() {}
void tearDown() {}

static Crypto makeCrypto(uint8_t seed) {
    uint8_t key[Crypto::KEY_LEN];
    for (size_t i = 0; i < Crypto::KEY_LEN; ++i) key[i] = uint8_t(seed + i);
    Crypto c;
    c.begin(key);
    return c;
}

static void makeNonce(uint8_t out[Crypto::NONCE_LEN], uint8_t seed) {
    for (size_t i = 0; i < Crypto::NONCE_LEN; ++i) out[i] = uint8_t(seed + i);
}

void test_encode_decode_data_roundtrip() {
    Crypto crypto = makeCrypto(1);
    uint8_t nonce[Crypto::NONCE_LEN];
    makeNonce(nonce, 0xA0);
    const uint8_t payload = '.';

    uint8_t buf[Packet::MAX_WIRE_LEN];
    const size_t len = Packet::encode(crypto, PacketType::DATA, /*src=*/1, /*dst=*/2,
                                       /*seq=*/42, nonce, &payload, 1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(Packet::HEADER_LEN + 1 + Crypto::TAG_LEN, len);

    PacketHeader header;
    uint8_t payloadOut[MAX_PAYLOAD_LEN] = {0};
    const bool ok = Packet::decode(crypto, buf, len, &header, payloadOut);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(uint8_t(PacketType::DATA), uint8_t(header.type));
    TEST_ASSERT_EQUAL_UINT8(1, header.src);
    TEST_ASSERT_EQUAL_UINT8(2, header.dst);
    TEST_ASSERT_EQUAL_UINT32(42, header.seq);
    TEST_ASSERT_EQUAL_UINT8(1, header.payloadLen);
    TEST_ASSERT_EQUAL_UINT8('.', payloadOut[0]);
}

void test_encode_decode_ack_roundtrip() {
    Crypto crypto = makeCrypto(2);
    uint8_t nonce[Crypto::NONCE_LEN];
    makeNonce(nonce, 0xB0);

    uint8_t buf[Packet::MAX_WIRE_LEN];
    const size_t len = Packet::encode(crypto, PacketType::ACK, /*src=*/2, /*dst=*/1,
                                       /*seq=*/7, nonce, nullptr, 0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(Packet::MIN_WIRE_LEN, len);

    PacketHeader header;
    uint8_t payloadOut[MAX_PAYLOAD_LEN] = {0};
    const bool ok = Packet::decode(crypto, buf, len, &header, payloadOut);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(uint8_t(PacketType::ACK), uint8_t(header.type));
    TEST_ASSERT_EQUAL_UINT32(7, header.seq);
    TEST_ASSERT_EQUAL_UINT8(0, header.payloadLen);
}

void test_decode_rejects_forged_packet_without_key() {
    Crypto sender = makeCrypto(3);
    Crypto attacker = makeCrypto(4);  // different key -- doesn't know the PSK
    uint8_t nonce[Crypto::NONCE_LEN];
    makeNonce(nonce, 0xC0);
    const uint8_t payload = '-';

    uint8_t buf[Packet::MAX_WIRE_LEN];
    const size_t len = Packet::encode(sender, PacketType::DATA, 1, 2, 5, nonce, &payload, 1, buf,
                                       sizeof(buf));

    PacketHeader header;
    uint8_t payloadOut[MAX_PAYLOAD_LEN] = {0};
    const bool ok = Packet::decode(attacker, buf, len, &header, payloadOut);
    TEST_ASSERT_FALSE(ok);
}

void test_decode_rejects_truncated_frame() {
    Crypto crypto = makeCrypto(5);
    uint8_t nonce[Crypto::NONCE_LEN];
    makeNonce(nonce, 0xD0);
    const uint8_t payload = '/';

    uint8_t buf[Packet::MAX_WIRE_LEN];
    const size_t len = Packet::encode(crypto, PacketType::DATA, 1, 2, 9, nonce, &payload, 1, buf,
                                       sizeof(buf));

    PacketHeader header;
    uint8_t payloadOut[MAX_PAYLOAD_LEN] = {0};
    const bool ok = Packet::decode(crypto, buf, len - 1, &header, payloadOut);
    TEST_ASSERT_FALSE(ok);
}

void test_decode_rejects_bad_version() {
    Crypto crypto = makeCrypto(6);
    uint8_t nonce[Crypto::NONCE_LEN];
    makeNonce(nonce, 0xE0);
    const uint8_t payload = '.';

    uint8_t buf[Packet::MAX_WIRE_LEN];
    const size_t len = Packet::encode(crypto, PacketType::DATA, 1, 2, 1, nonce, &payload, 1, buf,
                                       sizeof(buf));
    buf[0] = PROTOCOL_VERSION + 1;  // corrupt version byte (also breaks the tag, as intended)

    PacketHeader header;
    uint8_t payloadOut[MAX_PAYLOAD_LEN] = {0};
    const bool ok = Packet::decode(crypto, buf, len, &header, payloadOut);
    TEST_ASSERT_FALSE(ok);
}

void test_encode_rejects_oversized_payload() {
    Crypto crypto = makeCrypto(7);
    uint8_t nonce[Crypto::NONCE_LEN] = {0};
    uint8_t payload[MAX_PAYLOAD_LEN + 1] = {0};
    uint8_t buf[Packet::MAX_WIRE_LEN + 16];
    const size_t len = Packet::encode(crypto, PacketType::DATA, 1, 2, 1, nonce, payload,
                                       MAX_PAYLOAD_LEN + 1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(0, len);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_encode_decode_data_roundtrip);
    RUN_TEST(test_encode_decode_ack_roundtrip);
    RUN_TEST(test_decode_rejects_forged_packet_without_key);
    RUN_TEST(test_decode_rejects_truncated_frame);
    RUN_TEST(test_decode_rejects_bad_version);
    RUN_TEST(test_encode_rejects_oversized_payload);
    return UNITY_END();
}
