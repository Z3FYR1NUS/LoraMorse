#pragma once
// Crypto -- authenticated encryption primitive for LORA-CW packets.
//
// Replaces the previous custom scheme (AES-128-ECB(nonce) used as a one-shot
// PRF, XORed with a single plaintext byte, with byte 1 of the keystream as
// an 8-bit "tag"). That construction gave only ~8 bits of forgery
// resistance and no replay protection.
//
// This is a standard Encrypt-then-MAC construction built from primitives
// already bundled with the ESP32 Arduino core's mbedTLS (mbedtls/aes.h,
// mbedtls/md.h) and also available on desktop via libmbedtls-dev, so the
// same code can be unit-tested natively:
//
//   ciphertext = AES-128-CTR(key, nonce || counter=0, plaintext)
//   tag        = truncate(HMAC-SHA256(key, associatedData || ciphertext), TAG_LEN)
//
// The associated data (packet header: version/type/src/dst/seq/nonce/len) is
// authenticated but not encrypted -- the receiver needs it in the clear to
// even know how to parse and route the packet. Only the small payload is
// confidentiality-protected.
//
// A 96-bit (12-byte) random nonce is required per packet. AES-CTR must never
// reuse a (key, nonce, counter) triple; combined with the persisted,
// monotonically increasing sequence number (see ReplayGuard / App), this
// gives strong practical protection against keystream reuse even across
// reboots, in addition to the randomness itself.

#include <stddef.h>
#include <stdint.h>

class Crypto {
public:
    static constexpr size_t KEY_LEN   = 16;  // AES-128
    static constexpr size_t NONCE_LEN = 12;  // 96-bit nonce, per-packet random
    static constexpr size_t TAG_LEN   = 10;  // truncated HMAC-SHA256 (80-bit)

    // Loads the 16-byte AES key. Must be called once before encrypt/verify.
    void begin(const uint8_t key[KEY_LEN]);

    // Encrypts `plaintextLen` bytes of `plaintext` (0..maxPayload) under
    // `nonce`, authenticating `aad`/`aadLen` (the packet header) together
    // with the ciphertext. Writes ciphertext (same length as plaintext) to
    // `ciphertextOut` and the truncated tag to `tagOut`. `plaintext` and
    // `ciphertextOut` may alias.
    void encrypt(const uint8_t nonce[NONCE_LEN],
                 const uint8_t* aad, size_t aadLen,
                 const uint8_t* plaintext, size_t plaintextLen,
                 uint8_t* ciphertextOut, uint8_t tagOut[TAG_LEN]) const;

    // Recomputes the tag over `aad`+`ciphertext` and compares it
    // (constant-time) against `tag`. Only decrypts (writing to
    // `plaintextOut`) and returns true if the tag matches; on mismatch,
    // returns false and does not touch `plaintextOut`, so callers can never
    // accidentally consume unauthenticated plaintext.
    bool decryptAndVerify(const uint8_t nonce[NONCE_LEN],
                           const uint8_t* aad, size_t aadLen,
                           const uint8_t* ciphertext, size_t ciphertextLen,
                           const uint8_t tag[TAG_LEN],
                           uint8_t* plaintextOut) const;

private:
    uint8_t key_[KEY_LEN] = {0};

    void hmac(const uint8_t* aad, size_t aadLen,
              const uint8_t* ciphertext, size_t ciphertextLen,
              uint8_t fullTagOut[32]) const;
};
