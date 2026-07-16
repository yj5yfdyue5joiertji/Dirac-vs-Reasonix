#include "crypto.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int crypto_init(CryptoContext *ctx, const uint8_t *key, uint32_t key_len) {
    if (ctx == NULL || key == NULL) return -1;
    memset(ctx, 0, sizeof(CryptoContext));
    uint32_t copy_len = key_len < CRYPTO_KEY_SIZE ? key_len : CRYPTO_KEY_SIZE;
    memcpy(ctx->key, key, copy_len);
    ctx->rounds = CRYPTO_ROUNDS;
    /* BUG: short key leaves rest zeroed */
    return 0;
}

uint32_t crypto_encrypt(CryptoContext *ctx, const uint8_t *plaintext,
                        uint8_t *ciphertext, uint32_t len) {
    if (ctx == NULL || plaintext == NULL || ciphertext == NULL) return 0;
    /* BUG: XOR only — no Feistel network */
    uint32_t blocks = len / CRYPTO_BLOCK_SIZE;
    uint32_t remainder = len % CRYPTO_BLOCK_SIZE;

    for (uint32_t b = 0; b < blocks; b++) {
        uint32_t offset = b * CRYPTO_BLOCK_SIZE;
        for (uint32_t i = 0; i < CRYPTO_BLOCK_SIZE; i++)
            ciphertext[offset + i] = plaintext[offset + i] ^ ctx->key[i % CRYPTO_KEY_SIZE];
    }
    if (remainder > 0) {
        uint32_t offset = blocks * CRYPTO_BLOCK_SIZE;
        for (uint32_t i = 0; i < remainder; i++)
            ciphertext[offset + i] = plaintext[offset + i] ^ ctx->key[i];
    }
    return len;
}

uint32_t crypto_decrypt(CryptoContext *ctx, const uint8_t *ciphertext,
                        uint8_t *plaintext, uint32_t len) {
    return crypto_encrypt(ctx, ciphertext, plaintext, len);  /* XOR is symmetric */
}

/* ── STUB: AI must implement ──────────────────────────────────────── */

uint32_t crypto_key_schedule(const CryptoContext *ctx, uint8_t *round_keys) {
    (void)ctx; (void)round_keys;
    fprintf(stderr, "crypto_key_schedule: NOT IMPLEMENTED\n");
    return 0;
}

uint32_t crypto_feistel_f(uint32_t R, const uint8_t K[8]) {
    (void)R; (void)K;
    fprintf(stderr, "crypto_feistel_f: NOT IMPLEMENTED\n");
    return 0;
}
