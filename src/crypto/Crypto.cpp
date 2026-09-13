#include "Crypto.h"

#include <string.h>

#include "mbedtls/aes.h"
#include "mbedtls/md.h"

void Crypto::begin(const uint8_t key[KEY_LEN]) {
    memcpy(key_, key, KEY_LEN);
}

void Crypto::hmac(const uint8_t* aad, size_t aadLen,
                   const uint8_t* ciphertext, size_t ciphertextLen,
                   uint8_t fullTagOut[32]) const {
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, /*hmac=*/1);
    mbedtls_md_hmac_starts(&ctx, key_, KEY_LEN);
    if (aadLen) mbedtls_md_hmac_update(&ctx, aad, aadLen);
    if (ciphertextLen) mbedtls_md_hmac_update(&ctx, ciphertext, ciphertextLen);
    mbedtls_md_hmac_finish(&ctx, fullTagOut);
    mbedtls_md_free(&ctx);
}

void Crypto::encrypt(const uint8_t nonce[NONCE_LEN],
                      const uint8_t* aad, size_t aadLen,
                      const uint8_t* plaintext, size_t plaintextLen,
                      uint8_t* ciphertextOut, uint8_t tagOut[TAG_LEN]) const {
    // Build the 16-byte CTR nonce_counter block: 12-byte nonce || 32-bit
    // big-endian block counter, starting at 0. A single packet's payload is
    // at most a couple of bytes, so it never spans more than one 16-byte AES
    // block/counter value.
    uint8_t nonceCounter[16];
    memcpy(nonceCounter, nonce, NONCE_LEN);
    memset(nonceCounter + NONCE_LEN, 0, 4);

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, key_, 128);
    size_t ncOff = 0;
    uint8_t streamBlock[16] = {0};
    if (plaintextLen) {
        mbedtls_aes_crypt_ctr(&aes, plaintextLen, &ncOff, nonceCounter, streamBlock,
                               plaintext, ciphertextOut);
    }
    mbedtls_aes_free(&aes);

    uint8_t fullTag[32];
    hmac(aad, aadLen, ciphertextOut, plaintextLen, fullTag);
    memcpy(tagOut, fullTag, TAG_LEN);
}

bool Crypto::decryptAndVerify(const uint8_t nonce[NONCE_LEN],
                               const uint8_t* aad, size_t aadLen,
                               const uint8_t* ciphertext, size_t ciphertextLen,
                               const uint8_t tag[TAG_LEN],
                               uint8_t* plaintextOut) const {
    uint8_t fullTag[32];
    hmac(aad, aadLen, ciphertext, ciphertextLen, fullTag);

    // Constant-time comparison: always touch every byte, don't short-circuit
    // on the first mismatch, so timing doesn't leak how many leading bytes
    // of a forged tag happened to be correct.
    uint8_t diff = 0;
    for (size_t i = 0; i < TAG_LEN; ++i) diff |= uint8_t(fullTag[i] ^ tag[i]);
    if (diff != 0) return false;

    if (ciphertextLen) {
        uint8_t nonceCounter[16];
        memcpy(nonceCounter, nonce, NONCE_LEN);
        memset(nonceCounter + NONCE_LEN, 0, 4);

        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        mbedtls_aes_setkey_enc(&aes, key_, 128);  // CTR mode always uses the encrypt schedule.
        size_t ncOff = 0;
        uint8_t streamBlock[16] = {0};
        mbedtls_aes_crypt_ctr(&aes, ciphertextLen, &ncOff, nonceCounter, streamBlock,
                               ciphertext, plaintextOut);
        mbedtls_aes_free(&aes);
    }
    return true;
}
