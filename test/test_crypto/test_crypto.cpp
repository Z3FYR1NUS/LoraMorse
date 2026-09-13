#include <string.h>
#include <unity.h>

#include "crypto/Crypto.h"

void setUp() {}
void tearDown() {}

static void fillKey(uint8_t key[Crypto::KEY_LEN], uint8_t seed) {
    for (size_t i = 0; i < Crypto::KEY_LEN; ++i) key[i] = uint8_t(seed + i);
}

void test_roundtrip_recovers_plaintext() {
    uint8_t key[Crypto::KEY_LEN];
    fillKey(key, 0x11);
    Crypto crypto;
    crypto.begin(key);

    uint8_t nonce[Crypto::NONCE_LEN] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    const uint8_t aad[4] = {1, 2, 3, 4};
    const uint8_t plaintext[1] = {'.'};

    uint8_t ciphertext[1];
    uint8_t tag[Crypto::TAG_LEN];
    crypto.encrypt(nonce, aad, sizeof(aad), plaintext, 1, ciphertext, tag);

    uint8_t decoded[1] = {0};
    const bool ok =
        crypto.decryptAndVerify(nonce, aad, sizeof(aad), ciphertext, 1, tag, decoded);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8('.', decoded[0]);
}

void test_ciphertext_differs_from_plaintext() {
    uint8_t key[Crypto::KEY_LEN];
    fillKey(key, 0x22);
    Crypto crypto;
    crypto.begin(key);

    uint8_t nonce[Crypto::NONCE_LEN] = {0};
    const uint8_t aad[1] = {0xAA};
    const uint8_t plaintext[1] = {'-'};
    uint8_t ciphertext[1];
    uint8_t tag[Crypto::TAG_LEN];
    crypto.encrypt(nonce, aad, sizeof(aad), plaintext, 1, ciphertext, tag);

    // A well-formed stream cipher should not leave a single-byte plaintext
    // unchanged except by coincidence; this is a smoke check, not a proof.
    TEST_ASSERT_NOT_EQUAL(plaintext[0], ciphertext[0]);
}

void test_tampered_ciphertext_is_rejected() {
    uint8_t key[Crypto::KEY_LEN];
    fillKey(key, 0x33);
    Crypto crypto;
    crypto.begin(key);

    uint8_t nonce[Crypto::NONCE_LEN] = {9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};
    const uint8_t aad[2] = {5, 6};
    const uint8_t plaintext[1] = {'/'};
    uint8_t ciphertext[1];
    uint8_t tag[Crypto::TAG_LEN];
    crypto.encrypt(nonce, aad, sizeof(aad), plaintext, 1, ciphertext, tag);

    ciphertext[0] ^= 0x01;  // flip one bit
    uint8_t decoded[1] = {0};
    const bool ok =
        crypto.decryptAndVerify(nonce, aad, sizeof(aad), ciphertext, 1, tag, decoded);
    TEST_ASSERT_FALSE(ok);
}

void test_tampered_header_is_rejected() {
    uint8_t key[Crypto::KEY_LEN];
    fillKey(key, 0x44);
    Crypto crypto;
    crypto.begin(key);

    uint8_t nonce[Crypto::NONCE_LEN] = {0};
    uint8_t aad[2] = {5, 6};
    const uint8_t plaintext[1] = {' '};
    uint8_t ciphertext[1];
    uint8_t tag[Crypto::TAG_LEN];
    crypto.encrypt(nonce, aad, sizeof(aad), plaintext, 1, ciphertext, tag);

    aad[0] ^= 0x01;  // tamper with associated data (e.g. forged src/dst/seq)
    uint8_t decoded[1] = {0};
    const bool ok =
        crypto.decryptAndVerify(nonce, aad, sizeof(aad), ciphertext, 1, tag, decoded);
    TEST_ASSERT_FALSE(ok);
}

void test_wrong_key_is_rejected() {
    uint8_t keyA[Crypto::KEY_LEN], keyB[Crypto::KEY_LEN];
    fillKey(keyA, 0x55);
    fillKey(keyB, 0x56);
    Crypto sender, attacker;
    sender.begin(keyA);
    attacker.begin(keyB);

    uint8_t nonce[Crypto::NONCE_LEN] = {7};
    const uint8_t aad[1] = {1};
    const uint8_t plaintext[1] = {'.'};
    uint8_t ciphertext[1];
    uint8_t tag[Crypto::TAG_LEN];
    sender.encrypt(nonce, aad, sizeof(aad), plaintext, 1, ciphertext, tag);

    uint8_t decoded[1] = {0};
    const bool ok =
        attacker.decryptAndVerify(nonce, aad, sizeof(aad), ciphertext, 1, tag, decoded);
    TEST_ASSERT_FALSE(ok);
}

void test_different_nonces_give_different_ciphertext() {
    uint8_t key[Crypto::KEY_LEN];
    fillKey(key, 0x66);
    Crypto crypto;
    crypto.begin(key);

    const uint8_t aad[1] = {0};
    const uint8_t plaintext[1] = {'.'};

    uint8_t nonceA[Crypto::NONCE_LEN] = {0};
    uint8_t nonceB[Crypto::NONCE_LEN] = {0};
    nonceB[11] = 1;

    uint8_t ctA[1], ctB[1], tagA[Crypto::TAG_LEN], tagB[Crypto::TAG_LEN];
    crypto.encrypt(nonceA, aad, sizeof(aad), plaintext, 1, ctA, tagA);
    crypto.encrypt(nonceB, aad, sizeof(aad), plaintext, 1, ctB, tagB);

    TEST_ASSERT_TRUE(ctA[0] != ctB[0] || memcmp(tagA, tagB, Crypto::TAG_LEN) != 0);
}

void test_zero_length_payload_roundtrips() {
    uint8_t key[Crypto::KEY_LEN];
    fillKey(key, 0x77);
    Crypto crypto;
    crypto.begin(key);

    uint8_t nonce[Crypto::NONCE_LEN] = {3};
    const uint8_t aad[3] = {1, 2, 3};
    uint8_t tag[Crypto::TAG_LEN];
    crypto.encrypt(nonce, aad, sizeof(aad), nullptr, 0, nullptr, tag);

    const bool ok = crypto.decryptAndVerify(nonce, aad, sizeof(aad), nullptr, 0, tag, nullptr);
    TEST_ASSERT_TRUE(ok);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_roundtrip_recovers_plaintext);
    RUN_TEST(test_ciphertext_differs_from_plaintext);
    RUN_TEST(test_tampered_ciphertext_is_rejected);
    RUN_TEST(test_tampered_header_is_rejected);
    RUN_TEST(test_wrong_key_is_rejected);
    RUN_TEST(test_different_nonces_give_different_ciphertext);
    RUN_TEST(test_zero_length_payload_roundtrips);
    return UNITY_END();
}
